/*
 * Copyright © 2018 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "igt.h"
#include "sw_sync.h"
#include "igt_syncobj.h"
#include <unistd.h>
#include <time.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <signal.h>
#include "drm.h"

IGT_TEST_DESCRIPTION("Tests for the drm timeline sync object API");

/* One tenth of a second */
#define SHORT_TIME_NSEC 100000000ull

#define NSECS_PER_SEC 1000000000ull

static uint64_t
gettime_ns(void)
{
	struct timespec current;
	clock_gettime(CLOCK_MONOTONIC, &current);
	return (uint64_t)current.tv_sec * NSECS_PER_SEC + current.tv_nsec;
}

static void
sleep_nsec(uint64_t time_nsec)
{
	struct timespec t;
	t.tv_sec = time_nsec / NSECS_PER_SEC;
	t.tv_nsec = time_nsec % NSECS_PER_SEC;
	igt_assert_eq(nanosleep(&t, NULL), 0);
}

static uint64_t
short_timeout(void)
{
	return gettime_ns() + SHORT_TIME_NSEC;
}

static int
syncobj_attach_sw_sync(int fd, uint32_t handle, uint64_t point)
{
	struct drm_syncobj_handle;
	uint32_t syncobj = syncobj_create(fd, 0);
	int timeline, fence;

	timeline = sw_sync_timeline_create();
	fence = sw_sync_timeline_create_fence(timeline, 1);
	syncobj_import_sync_file(fd, syncobj, fence);
	syncobj_binary_to_timeline(fd, handle, point, syncobj);
	close(fence);

	syncobj_destroy(fd, syncobj);
	return timeline;
}

static void
syncobj_trigger(int fd, uint32_t handle, uint64_t point)
{
	int timeline = syncobj_attach_sw_sync(fd, handle, point);
	sw_sync_timeline_inc(timeline, 1);
	close(timeline);
}

static timer_t
set_timer(void (*cb)(union sigval), void *ptr, int i, uint64_t nsec)
{
        timer_t timer;
        struct sigevent sev;
        struct itimerspec its;

        memset(&sev, 0, sizeof(sev));
        sev.sigev_notify = SIGEV_THREAD;
	if (ptr)
		sev.sigev_value.sival_ptr = ptr;
	else
		sev.sigev_value.sival_int = i;
        sev.sigev_notify_function = cb;
        igt_assert(timer_create(CLOCK_MONOTONIC, &sev, &timer) == 0);

        memset(&its, 0, sizeof(its));
        its.it_value.tv_sec = nsec / NSEC_PER_SEC;
        its.it_value.tv_nsec = nsec % NSEC_PER_SEC;
        igt_assert(timer_settime(timer, 0, &its, NULL) == 0);

	return timer;
}

struct fd_handle_pair {
	int fd;
	uint32_t handle;
	uint64_t point;
};

static void
timeline_inc_func(union sigval sigval)
{
	sw_sync_timeline_inc(sigval.sival_int, 1);
}

static void
syncobj_trigger_free_pair_func(union sigval sigval)
{
	struct fd_handle_pair *pair = sigval.sival_ptr;
	syncobj_trigger(pair->fd, pair->handle, pair->point);
	free(pair);
}

static timer_t
syncobj_trigger_delayed(int fd, uint32_t syncobj, uint64_t point, uint64_t nsec)
{
	struct fd_handle_pair *pair = malloc(sizeof(*pair));

	pair->fd = fd;
	pair->handle = syncobj;
	pair->point = point;

	return set_timer(syncobj_trigger_free_pair_func, pair, 0, nsec);
}

static void
test_wait_bad_flags(int fd)
{
	struct drm_syncobj_timeline_wait wait = { 0 };
	wait.flags = 0xdeadbeef;
	igt_assert_eq(__syncobj_timeline_wait_ioctl(fd, &wait), -EINVAL);
}

static void
test_wait_zero_handles(int fd)
{
	struct drm_syncobj_timeline_wait wait = { 0 };
	igt_assert_eq(__syncobj_timeline_wait_ioctl(fd, &wait), -EINVAL);
}

static void
test_wait_illegal_handle(int fd)
{
	struct drm_syncobj_timeline_wait wait = { 0 };
	uint32_t handle = 0;

	wait.count_handles = 1;
	wait.handles = to_user_pointer(&handle);
	igt_assert_eq(__syncobj_timeline_wait_ioctl(fd, &wait), -ENOENT);
}

static void
test_query_zero_handles(int fd)
{
	struct drm_syncobj_timeline_array args = { 0 };
	int ret;

	ret = drmIoctl(fd, DRM_IOCTL_SYNCOBJ_QUERY, &args);
	igt_assert(ret == -1 && errno ==  EINVAL);
}

static void
test_query_illegal_handle(int fd)
{
	struct drm_syncobj_timeline_array args = { 0 };
	uint32_t handle = 0;
	int ret;

	args.count_handles = 1;
	args.handles = to_user_pointer(&handle);
	ret = drmIoctl(fd, DRM_IOCTL_SYNCOBJ_QUERY, &args);
	igt_assert(ret == -1 && errno == ENOENT);
}

static void
test_query_one_illegal_handle(int fd)
{
	struct drm_syncobj_timeline_array array = { 0 };
	uint32_t syncobjs[3];
	uint64_t initial_point = 1;
	int ret;

	syncobjs[0] = syncobj_create(fd, 0);
	syncobjs[1] = 0;
	syncobjs[2] = syncobj_create(fd, 0);

	syncobj_timeline_signal(fd, &syncobjs[0], &initial_point, 1);
	syncobj_timeline_signal(fd, &syncobjs[2], &initial_point, 1);
	igt_assert_eq(syncobj_timeline_wait_err(fd, &syncobjs[0],
						&initial_point, 1, 0, 0), 0);
	igt_assert_eq(syncobj_timeline_wait_err(fd, &syncobjs[2],
						&initial_point, 1, 0, 0), 0);

	array.count_handles = 3;
	array.handles = to_user_pointer(syncobjs);
	ret = drmIoctl(fd, DRM_IOCTL_SYNCOBJ_QUERY, &array);
	igt_assert(ret == -1 && errno == ENOENT);

	syncobj_destroy(fd, syncobjs[0]);
	syncobj_destroy(fd, syncobjs[2]);
}

static void
test_query_bad_pad(int fd)
{
	struct drm_syncobj_timeline_array array = { 0 };
	uint32_t handle = 0;
	int ret;

	array.pad = 0xdeadbeef;
	array.count_handles = 1;
	array.handles = to_user_pointer(&handle);
	ret = drmIoctl(fd, DRM_IOCTL_SYNCOBJ_QUERY, &array);
	igt_assert(ret == -1 && errno == EINVAL);
}

static void
test_signal_zero_handles(int fd)
{
	struct drm_syncobj_timeline_array args = { 0 };
	int ret;

	ret = drmIoctl(fd, DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL, &args);
	igt_assert(ret == -1 && errno ==  EINVAL);
}

static void
test_signal_illegal_handle(int fd)
{
	struct drm_syncobj_timeline_array args = { 0 };
	uint32_t handle = 0;
	int ret;

	args.count_handles = 1;
	args.handles = to_user_pointer(&handle);
	ret = drmIoctl(fd, DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL, &args);
	igt_assert(ret == -1 && errno == ENOENT);
}

static void
test_signal_illegal_point(int fd)
{
	struct drm_syncobj_timeline_array args = { 0 };
	uint32_t handle = 1;
	uint64_t point = 0;
	int ret;

	args.count_handles = 1;
	args.handles = to_user_pointer(&handle);
	args.points = to_user_pointer(&point);
	ret = drmIoctl(fd, DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL, &args);
	igt_assert(ret == -1 && errno == ENOENT);
}
static void
test_signal_one_illegal_handle(int fd)
{
	struct drm_syncobj_timeline_array array = { 0 };
	uint32_t syncobjs[3];
	uint64_t initial_point = 1;
	int ret;

	syncobjs[0] = syncobj_create(fd, 0);
	syncobjs[1] = 0;
	syncobjs[2] = syncobj_create(fd, 0);

	syncobj_timeline_signal(fd, &syncobjs[0], &initial_point, 1);
	syncobj_timeline_signal(fd, &syncobjs[2], &initial_point, 1);
	igt_assert_eq(syncobj_timeline_wait_err(fd, &syncobjs[0],
						&initial_point, 1, 0, 0), 0);
	igt_assert_eq(syncobj_timeline_wait_err(fd, &syncobjs[2],
						&initial_point, 1, 0, 0), 0);

	array.count_handles = 3;
	array.handles = to_user_pointer(syncobjs);
	ret = drmIoctl(fd, DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL, &array);
	igt_assert(ret == -1 && errno == ENOENT);

	syncobj_destroy(fd, syncobjs[0]);
	syncobj_destroy(fd, syncobjs[2]);
}

static void
test_signal_bad_pad(int fd)
{
	struct drm_syncobj_timeline_array array = { 0 };
	uint32_t handle = 0;
	int ret;

	array.pad = 0xdeadbeef;
	array.count_handles = 1;
	array.handles = to_user_pointer(&handle);
	ret = drmIoctl(fd, DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL, &array);
	igt_assert(ret == -1 && errno == EINVAL);
}

static void
test_signal_array(int fd)
{
	uint32_t syncobjs[4];
	uint64_t points[4] = {1, 1, 1, 0};

	syncobjs[0] = syncobj_create(fd, 0);
	syncobjs[1] = syncobj_create(fd, 0);
	syncobjs[2] = syncobj_create(fd, 0);
	syncobjs[3] = syncobj_create(fd, 0);

	syncobj_timeline_signal(fd, syncobjs, points, 4);
	igt_assert_eq(syncobj_timeline_wait_err(fd, syncobjs,
						points, 3, 0, 0), 0);
	igt_assert_eq(syncobj_wait_err(fd, &syncobjs[3], 1, 0, 0), 0);

	syncobj_destroy(fd, syncobjs[0]);
	syncobj_destroy(fd, syncobjs[1]);
	syncobj_destroy(fd, syncobjs[2]);
	syncobj_destroy(fd, syncobjs[3]);
}

static void
test_transfer_illegal_handle(int fd)
{
	struct drm_syncobj_transfer args = { 0 };
	uint32_t handle = 0;
	int ret;

	args.src_handle = to_user_pointer(&handle);
	args.dst_handle = to_user_pointer(&handle);
	args.src_point = 1;
	args.dst_point = 0;
	ret = drmIoctl(fd, DRM_IOCTL_SYNCOBJ_TRANSFER, &args);
	igt_assert(ret == -1 && errno == ENOENT);
}

static void
test_transfer_bad_pad(int fd)
{
	struct drm_syncobj_transfer arg = { 0 };
	uint32_t handle = 0;
	int ret;

	arg.pad = 0xdeadbeef;
	arg.src_handle = to_user_pointer(&handle);
	arg.dst_handle = to_user_pointer(&handle);
	ret = drmIoctl(fd, DRM_IOCTL_SYNCOBJ_TRANSFER, &arg);
	igt_assert(ret == -1 && errno == EINVAL);
}

#define WAIT_FOR_SUBMIT		(1 << 0)
#define WAIT_ALL		(1 << 1)
#define WAIT_AVAILABLE		(1 << 2)
#define WAIT_UNSUBMITTED	(1 << 3)
#define WAIT_SUBMITTED		(1 << 4)
#define WAIT_SIGNALED		(1 << 5)
#define WAIT_FLAGS_MAX		(1 << 6) - 1

static uint32_t
flags_for_test_flags(uint32_t test_flags)
{
	uint32_t flags = 0;

	if (test_flags & WAIT_FOR_SUBMIT)
		flags |= DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;

	if (test_flags & WAIT_AVAILABLE)
		flags |= DRM_SYNCOBJ_WAIT_FLAGS_WAIT_AVAILABLE;

	if (test_flags & WAIT_ALL)
		flags |= DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL;

	return flags;
}

static void
test_single_wait(int fd, uint32_t test_flags, int expect)
{
	uint32_t syncobj = syncobj_create(fd, 0);
	uint32_t flags = flags_for_test_flags(test_flags);
	uint64_t point = 1;
	int timeline = -1;

	if (test_flags & (WAIT_SUBMITTED | WAIT_SIGNALED))
		timeline = syncobj_attach_sw_sync(fd, syncobj, point);

	if (test_flags & WAIT_SIGNALED)
		sw_sync_timeline_inc(timeline, 1);

	igt_assert_eq(syncobj_timeline_wait_err(fd, &syncobj, &point, 1,
						0, flags), expect);

	igt_assert_eq(syncobj_timeline_wait_err(fd, &syncobj, &point, 1,
						short_timeout(), flags), expect);

	if (expect != -ETIME) {
		igt_assert_eq(syncobj_timeline_wait_err(fd, &syncobj, &point, 1,
							UINT64_MAX, flags), expect);
	}

	syncobj_destroy(fd, syncobj);
	if (timeline != -1)
		close(timeline);
}

static void
test_wait_delayed_signal(int fd, uint32_t test_flags)
{
	uint32_t syncobj = syncobj_create(fd, 0);
	uint32_t flags = flags_for_test_flags(test_flags);
	uint64_t point = 1;
	int timeline = -1;
	timer_t timer;

	if (test_flags & WAIT_FOR_SUBMIT) {
		timer = syncobj_trigger_delayed(fd, syncobj, point, SHORT_TIME_NSEC);
	} else {
		timeline = syncobj_attach_sw_sync(fd, syncobj, point);
		timer = set_timer(timeline_inc_func, NULL,
				  timeline, SHORT_TIME_NSEC);
	}

	igt_assert(syncobj_timeline_wait(fd, &syncobj, &point, 1,
				gettime_ns() + SHORT_TIME_NSEC * 2,
				flags, NULL));

	timer_delete(timer);

	if (timeline != -1)
		close(timeline);

	syncobj_destroy(fd, syncobj);
}

static void
test_reset_unsignaled(int fd)
{
	uint32_t syncobj = syncobj_create(fd, 0);
	uint64_t point = 1;

	igt_assert_eq(syncobj_timeline_wait_err(fd, &syncobj, &point,
						1, 0, 0), -EINVAL);

	syncobj_reset(fd, &syncobj, 1);

	igt_assert_eq(syncobj_timeline_wait_err(fd, &syncobj, &point,
						1, 0, 0), -EINVAL);

	syncobj_destroy(fd, syncobj);
}

static void
test_reset_signaled(int fd)
{
	uint32_t syncobj = syncobj_create(fd, 0);
	uint64_t point = 1;

	syncobj_trigger(fd, syncobj, point);

	igt_assert_eq(syncobj_timeline_wait_err(fd, &syncobj, &point,
						1, 0, 0), 0);

	syncobj_reset(fd, &syncobj, 1);

	igt_assert_eq(syncobj_timeline_wait_err(fd, &syncobj, &point,
						1, 0, 0), -EINVAL);

	syncobj_destroy(fd, syncobj);
}

static void
test_reset_multiple_signaled(int fd)
{
	uint64_t points[3] = {1, 1, 1};
	uint32_t syncobjs[3];
	int i;

	for (i = 0; i < 3; i++) {
		syncobjs[i] = syncobj_create(fd, 0);
		syncobj_trigger(fd, syncobjs[i], points[i]);
	}

	igt_assert_eq(syncobj_timeline_wait_err(fd, syncobjs, points, 3, 0, 0), 0);

	syncobj_reset(fd, syncobjs, 3);

	for (i = 0; i < 3; i++) {
		igt_assert_eq(syncobj_timeline_wait_err(fd, &syncobjs[i],
							&points[i], 1,
							0, 0), -EINVAL);
		syncobj_destroy(fd, syncobjs[i]);
	}
}

static void
reset_and_trigger_func(union sigval sigval)
{
	struct fd_handle_pair *pair = sigval.sival_ptr;
	syncobj_reset(pair->fd, &pair->handle, 1);
	syncobj_trigger(pair->fd, pair->handle, pair->point);
}

static void
test_reset_during_wait_for_submit(int fd)
{
	uint32_t syncobj = syncobj_create(fd, 0);
	uint32_t flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;
	struct fd_handle_pair pair;
	uint64_t point = 1;
	timer_t timer;

	pair.fd = fd;
	pair.handle = syncobj;
	timer = set_timer(reset_and_trigger_func, &pair, 0, SHORT_TIME_NSEC);

	/* A reset should be a no-op even if we're in the middle of a wait */
	igt_assert(syncobj_timeline_wait(fd, &syncobj, &point, 1,
				gettime_ns() + SHORT_TIME_NSEC * 2,
				flags, NULL));

	timer_delete(timer);

	syncobj_destroy(fd, syncobj);
}

static void
test_signal(int fd)
{
	uint32_t syncobj = syncobj_create(fd, 0);
	uint32_t flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;
	uint64_t point = 1;

	igt_assert_eq(syncobj_timeline_wait_err(fd, &syncobj, &point,
						1, 0, 0), -EINVAL);
	igt_assert_eq(syncobj_timeline_wait_err(fd, &syncobj, &point,
						1, 0, flags), -ETIME);

	syncobj_timeline_signal(fd, &syncobj, &point, 1);

	igt_assert(syncobj_timeline_wait(fd, &syncobj, &point, 1, 0, 0, NULL));
	igt_assert(syncobj_timeline_wait(fd, &syncobj, &point, 1, 0, flags, NULL));

	syncobj_destroy(fd, syncobj);
}

static void
test_multi_wait(int fd, uint32_t test_flags, int expect)
{
	uint32_t syncobjs[3];
	uint32_t tflag, flags;
	int i, fidx, timeline;
	uint64_t points[3] = {1, 1, 1};

	syncobjs[0] = syncobj_create(fd, 0);
	syncobjs[1] = syncobj_create(fd, 0);
	syncobjs[2] = syncobj_create(fd, 0);

	flags = flags_for_test_flags(test_flags);
	test_flags &= ~(WAIT_ALL | WAIT_FOR_SUBMIT | WAIT_AVAILABLE);

	for (i = 0; i < 3; i++) {
		fidx = ffs(test_flags) - 1;
		tflag = (1 << fidx);

		if (test_flags & ~tflag)
			test_flags &= ~tflag;

		if (tflag & (WAIT_SUBMITTED | WAIT_SIGNALED))
			timeline = syncobj_attach_sw_sync(fd, syncobjs[i],
							  points[i]);
		if (tflag & WAIT_SIGNALED)
			sw_sync_timeline_inc(timeline, 1);
	}

	igt_assert_eq(syncobj_timeline_wait_err(fd, syncobjs, points, 3, 0, flags), expect);

	igt_assert_eq(syncobj_timeline_wait_err(fd, syncobjs, points, 3, short_timeout(),
				       flags), expect);

	if (expect != -ETIME) {
		igt_assert_eq(syncobj_timeline_wait_err(fd, syncobjs, points, 3, UINT64_MAX,
					       flags), expect);
	}

	syncobj_destroy(fd, syncobjs[0]);
	syncobj_destroy(fd, syncobjs[1]);
	syncobj_destroy(fd, syncobjs[2]);
}

struct wait_thread_data {
	int fd;
	struct drm_syncobj_timeline_wait wait;
};

static void *
wait_thread_func(void *data)
{
	struct wait_thread_data *wait = data;
	igt_assert_eq(__syncobj_timeline_wait_ioctl(wait->fd, &wait->wait), 0);
	return NULL;
}

static void
test_wait_snapshot(int fd, uint32_t test_flags)
{
	struct wait_thread_data wait = { 0 };
	uint32_t syncobjs[2];
	uint64_t points[2] = {1, 1};
	int timelines[3] = { -1, -1, -1 };
	pthread_t thread;

	syncobjs[0] = syncobj_create(fd, 0);
	syncobjs[1] = syncobj_create(fd, 0);

	if (!(test_flags & WAIT_FOR_SUBMIT)) {
		timelines[0] = syncobj_attach_sw_sync(fd, syncobjs[0], points[0]);
		timelines[1] = syncobj_attach_sw_sync(fd, syncobjs[1], points[1]);
	}

	wait.fd = fd;
	wait.wait.handles = to_user_pointer(syncobjs);
	wait.wait.count_handles = 2;
	wait.wait.points = to_user_pointer(points);
	wait.wait.timeout_nsec = short_timeout();
	wait.wait.flags = flags_for_test_flags(test_flags);

	igt_assert_eq(pthread_create(&thread, NULL, wait_thread_func, &wait), 0);

	sleep_nsec(SHORT_TIME_NSEC / 5);

	/* Try to fake the kernel out by triggering or partially triggering
	 * the first fence.
	 */
	if (test_flags & WAIT_ALL) {
		/* If it's WAIT_ALL, actually trigger it */
		if (timelines[0] == -1)
			syncobj_trigger(fd, syncobjs[0], points[0]);
		else
			sw_sync_timeline_inc(timelines[0], 1);
	} else if (test_flags & WAIT_FOR_SUBMIT) {
		timelines[0] = syncobj_attach_sw_sync(fd, syncobjs[0], points[0]);
	}

	sleep_nsec(SHORT_TIME_NSEC / 5);

	/* Then reset it */
	syncobj_reset(fd, &syncobjs[0], 1);

	sleep_nsec(SHORT_TIME_NSEC / 5);

	/* Then "submit" it in a way that will never trigger.  This way, if
	 * the kernel picks up on the new fence (it shouldn't), we'll get a
	 * timeout.
	 */
	timelines[2] = syncobj_attach_sw_sync(fd, syncobjs[0], points[0]);

	sleep_nsec(SHORT_TIME_NSEC / 5);

	/* Now trigger the second fence to complete the wait */

	if (timelines[1] == -1)
		syncobj_trigger(fd, syncobjs[1], points[1]);
	else
		sw_sync_timeline_inc(timelines[1], 1);

	pthread_join(thread, NULL);

	if (!(test_flags & WAIT_ALL))
		igt_assert_eq(wait.wait.first_signaled, 1);

	close(timelines[0]);
	close(timelines[1]);
	close(timelines[2]);
	syncobj_destroy(fd, syncobjs[0]);
	syncobj_destroy(fd, syncobjs[1]);
}

/* The numbers 0-7, each repeated 5x and shuffled. */
static const unsigned shuffled_0_7_x4[] = {
	2, 0, 6, 1, 1, 4, 5, 2, 0, 7, 1, 7, 6, 3, 4, 5,
	0, 2, 7, 3, 5, 4, 0, 6, 7, 3, 2, 5, 6, 1, 4, 3,
};

enum syncobj_stage {
	STAGE_UNSUBMITTED,
	STAGE_SUBMITTED,
	STAGE_SIGNALED,
	STAGE_RESET,
	STAGE_RESUBMITTED,
};

static void
test_wait_complex(int fd, uint32_t test_flags)
{
	struct wait_thread_data wait = { 0 };
	uint32_t syncobjs[8];
	uint64_t points[8] = {1, 1, 1, 1, 1, 1, 1, 1};
	enum syncobj_stage stage[8];
	int i, j, timelines[8];
	uint32_t first_signaled = -1, num_signaled = 0;
	pthread_t thread;

	for (i = 0; i < 8; i++) {
		stage[i] = STAGE_UNSUBMITTED;
		syncobjs[i] = syncobj_create(fd, 0);
	}

	if (test_flags & WAIT_FOR_SUBMIT) {
		for (i = 0; i < 8; i++)
			timelines[i] = -1;
	} else {
		for (i = 0; i < 8; i++)
			timelines[i] = syncobj_attach_sw_sync(fd, syncobjs[i],
							      points[i]);
	}

	wait.fd = fd;
	wait.wait.handles = to_user_pointer(syncobjs);
	wait.wait.count_handles = 2;
	wait.wait.points = to_user_pointer(points);
	wait.wait.timeout_nsec = gettime_ns() + NSECS_PER_SEC;
	wait.wait.flags = flags_for_test_flags(test_flags);

	igt_assert_eq(pthread_create(&thread, NULL, wait_thread_func, &wait), 0);

	sleep_nsec(NSECS_PER_SEC / 50);

	num_signaled = 0;
	for (j = 0; j < ARRAY_SIZE(shuffled_0_7_x4); j++) {
		i = shuffled_0_7_x4[j];
		igt_assert_lt(i, ARRAY_SIZE(syncobjs));

		switch (stage[i]++) {
		case STAGE_UNSUBMITTED:
			/* We need to submit attach a fence */
			if (!(test_flags & WAIT_FOR_SUBMIT)) {
				/* We had to attach one up-front */
				igt_assert_neq(timelines[i], -1);
				break;
			}
			timelines[i] = syncobj_attach_sw_sync(fd, syncobjs[i],
							      points[i]);
			break;

		case STAGE_SUBMITTED:
			/* We have a fence, trigger it */
			igt_assert_neq(timelines[i], -1);
			sw_sync_timeline_inc(timelines[i], 1);
			close(timelines[i]);
			timelines[i] = -1;
			if (num_signaled == 0)
				first_signaled = i;
			num_signaled++;
			break;

		case STAGE_SIGNALED:
			/* We're already signaled, reset */
			syncobj_reset(fd, &syncobjs[i], 1);
			break;

		case STAGE_RESET:
			/* We're reset, submit and don't signal */
			timelines[i] = syncobj_attach_sw_sync(fd, syncobjs[i],
							      points[i]);
			break;

		case STAGE_RESUBMITTED:
			igt_assert(!"Should not reach this stage");
			break;
		}

		if (test_flags & WAIT_ALL) {
			if (num_signaled == ARRAY_SIZE(syncobjs))
				break;
		} else {
			if (num_signaled > 0)
				break;
		}

		sleep_nsec(NSECS_PER_SEC / 100);
	}

	pthread_join(thread, NULL);

	if (test_flags & WAIT_ALL) {
		igt_assert_eq(num_signaled, ARRAY_SIZE(syncobjs));
	} else {
		igt_assert_eq(num_signaled, 1);
		igt_assert_eq(wait.wait.first_signaled, first_signaled);
	}

	for (i = 0; i < 8; i++) {
		close(timelines[i]);
		syncobj_destroy(fd, syncobjs[i]);
	}
}

static void
test_wait_interrupted(int fd, uint32_t test_flags)
{
	struct drm_syncobj_timeline_wait wait = { 0 };
	uint32_t syncobj = syncobj_create(fd, 0);
	uint64_t point = 1;
	int timeline;

	wait.handles = to_user_pointer(&syncobj);
	wait.points = to_user_pointer(&point);
	wait.count_handles = 1;
	wait.flags = flags_for_test_flags(test_flags);

	if (test_flags & WAIT_FOR_SUBMIT) {
		wait.timeout_nsec = short_timeout();
		igt_while_interruptible(true)
			igt_assert_eq(__syncobj_timeline_wait_ioctl(fd, &wait), -ETIME);
	}

	timeline = syncobj_attach_sw_sync(fd, syncobj, point);

	wait.timeout_nsec = short_timeout();
	igt_while_interruptible(true)
		igt_assert_eq(__syncobj_timeline_wait_ioctl(fd, &wait), -ETIME);

	syncobj_destroy(fd, syncobj);
	close(timeline);
}

/*
 * Verifies that as we signal points from the host, the syncobj
 * timeline value increments and that waits for submits/signals works
 * properly.
 */
static void
test_host_signal_points(int fd)
{
	uint32_t syncobj = syncobj_create(fd, 0);
	uint64_t value = 0;
	int i;

	for (i = 0; i < 100; i++) {
		uint64_t query_value = 0;

		value += rand();

		syncobj_timeline_signal(fd, &syncobj, &value, 1);

		syncobj_timeline_query(fd, &syncobj, &query_value, 1);
		igt_assert_eq(query_value, value);

		igt_assert(syncobj_timeline_wait(fd, &syncobj, &query_value,
						 1, 0, WAIT_FOR_SUBMIT, NULL));

		query_value -= 1;
		igt_assert(syncobj_timeline_wait(fd, &syncobj, &query_value,
						 1, 0, WAIT_ALL, NULL));
	}

	syncobj_destroy(fd, syncobj);
}

/*
 * Verifies that a device signaling fences out of order on the
 * timeline still increments the timeline monotonically and that waits
 * work properly.
 */
static void
test_device_signal_unordered(int fd)
{
	uint32_t syncobj = syncobj_create(fd, 0);
	int point_indices[] = { 0, 2, 1, 4, 3 };
	bool signaled[ARRAY_SIZE(point_indices)] = { 0 };
	int fences[ARRAY_SIZE(point_indices)];
	int timeline = sw_sync_timeline_create();
	uint64_t value = 0;
	int i, j;

	for (i = 0; i < ARRAY_SIZE(fences); i++) {
		fences[point_indices[i]] = sw_sync_timeline_create_fence(timeline, i + 1);
	}

	for (i = 0; i < ARRAY_SIZE(fences); i++) {
		uint32_t tmp_syncobj = syncobj_create(fd, 0);

		syncobj_import_sync_file(fd, tmp_syncobj, fences[i]);
		syncobj_binary_to_timeline(fd, syncobj, i + 1, tmp_syncobj);
		syncobj_destroy(fd, tmp_syncobj);
	}

	for (i = 0; i < ARRAY_SIZE(fences); i++) {
		uint64_t query_value = 0;
		uint64_t min_value = 0;

		sw_sync_timeline_inc(timeline, 1);

		signaled[point_indices[i]] = true;

		/*
		 * Compute a minimum value of the timeline based of
		 * the smallest signaled point.
		 */
		for (j = 0; j < ARRAY_SIZE(signaled); j++) {
			if (!signaled[j])
				break;
			min_value = j;
		}

		syncobj_timeline_query(fd, &syncobj, &query_value, 1);
		igt_assert(query_value >= min_value);
		igt_assert(query_value >= value);

		igt_debug("signaling point %i, timeline value = %" PRIu64 "\n",
			  point_indices[i] + 1, query_value);

		value = max(query_value, value);

		igt_assert(syncobj_timeline_wait(fd, &syncobj, &query_value,
						 1, 0, WAIT_FOR_SUBMIT, NULL));

		igt_assert(syncobj_timeline_wait(fd, &syncobj, &query_value,
						 1, 0, WAIT_ALL, NULL));
	}

	for (i = 0; i < ARRAY_SIZE(fences); i++)
		close(fences[i]);

	syncobj_destroy(fd, syncobj);
	close(timeline);
}

/*
 * Verifies that submitting out of order doesn't break the timeline.
 */
static void
test_device_submit_unordered(int fd)
{
	uint32_t syncobj = syncobj_create(fd, 0);
	uint64_t points[] = { 1, 5, 3, 6, 7 };
	int timeline = sw_sync_timeline_create();
	uint64_t query_value;
	int i;

	for (i = 0; i < ARRAY_SIZE(points); i++) {
		int fence = sw_sync_timeline_create_fence(timeline, i + 1);
		uint32_t tmp_syncobj = syncobj_create(fd, 0);

		syncobj_import_sync_file(fd, tmp_syncobj, fence);
		syncobj_binary_to_timeline(fd, syncobj, points[i], tmp_syncobj);
		close(fence);
		syncobj_destroy(fd, tmp_syncobj);
	}

	/*
	 * Signal points 1, 5 & 3. There are no other points <= 5 so
	 * waiting on 5 should return immediately for submission &
	 * signaling.
	 */
	sw_sync_timeline_inc(timeline, 3);

	syncobj_timeline_query(fd, &syncobj, &query_value, 1);
	igt_assert_eq(query_value, 5);

	igt_assert(syncobj_timeline_wait(fd, &syncobj, &query_value,
					 1, 0, WAIT_FOR_SUBMIT, NULL));

	igt_assert(syncobj_timeline_wait(fd, &syncobj, &query_value,
					 1, 0, WAIT_ALL, NULL));

	syncobj_destroy(fd, syncobj);
	close(timeline);
}

/*
 * Verifies that the host signaling fences out of order on the
 * timeline still increments the timeline monotonically and that waits
 * work properly.
 */
static void
test_host_signal_ordered(int fd)
{
	uint32_t syncobj = syncobj_create(fd, 0);
	int timeline = sw_sync_timeline_create();
	uint64_t host_signal_value = 8, query_value;
	int i;

	for (i = 0; i < 5; i++) {
		int fence = sw_sync_timeline_create_fence(timeline, i + 1);
		uint32_t tmp_syncobj = syncobj_create(fd, 0);

		syncobj_import_sync_file(fd, tmp_syncobj, fence);
		syncobj_binary_to_timeline(fd, syncobj, i + 1, tmp_syncobj);
		syncobj_destroy(fd, tmp_syncobj);
		close(fence);
	}

	sw_sync_timeline_inc(timeline, 3);

	syncobj_timeline_query(fd, &syncobj, &query_value, 1);
	igt_assert_eq(query_value, 3);

	syncobj_timeline_signal(fd, &syncobj, &host_signal_value, 1);

	syncobj_timeline_query(fd, &syncobj, &query_value, 1);
	igt_assert_eq(query_value, 3);

	sw_sync_timeline_inc(timeline, 5);

	syncobj_timeline_query(fd, &syncobj, &query_value, 1);
	igt_assert_eq(query_value, 8);

	syncobj_destroy(fd, syncobj);
	close(timeline);
}

/*
 * Verifies that host signaling  out of order doesn't break the timeline.
 */
static void
test_host_signal_unordered(int fd)
{
	uint32_t syncobj = syncobj_create(fd, 0);
	uint64_t points[] = { 1, 5 };
	uint64_t host_signal_value = 3;
	int timeline = sw_sync_timeline_create();
	uint64_t query_value;
	int i;

	for (i = 0; i < ARRAY_SIZE(points); i++) {
		int fence = sw_sync_timeline_create_fence(timeline, i + 1);
		uint32_t tmp_syncobj = syncobj_create(fd, 0);

		syncobj_import_sync_file(fd, tmp_syncobj, fence);
		syncobj_binary_to_timeline(fd, syncobj, points[i], tmp_syncobj);
		close(fence);
		syncobj_destroy(fd, tmp_syncobj);
	}

	syncobj_timeline_signal(fd, &syncobj, &host_signal_value, 1);

	syncobj_timeline_query(fd, &syncobj, &query_value, 1);
	igt_assert_eq(query_value, 0);

	sw_sync_timeline_inc(timeline, 1);

	syncobj_timeline_query(fd, &syncobj, &query_value, 1);
	igt_assert_eq(query_value, 3);

	sw_sync_timeline_inc(timeline, 1);

	syncobj_timeline_query(fd, &syncobj, &query_value, 1);
	igt_assert_eq(query_value, 5);

	igt_assert(syncobj_timeline_wait(fd, &syncobj, &query_value,
					 1, 0, WAIT_ALL, NULL));

	syncobj_destroy(fd, syncobj);
	close(timeline);
}

static bool
has_syncobj_timeline_wait(int fd)
{
	struct drm_syncobj_timeline_wait wait = { 0 };
	uint32_t handle = 0;
	uint64_t value;
	int ret;

	if (drmGetCap(fd, DRM_CAP_SYNCOBJ, &value))
		return false;
	if (!value)
		return false;

	/* Try waiting for zero sync objects should fail with EINVAL */
	wait.count_handles = 1;
	wait.handles = to_user_pointer(&handle);
	ret = drmIoctl(fd, DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT, &wait);
	return ret == -1 && errno == ENOENT;
}

igt_main
{
	int fd = -1;

	igt_fixture {
		fd = drm_open_driver(DRIVER_ANY);
		igt_require(has_syncobj_timeline_wait(fd));
		igt_require_sw_sync();
	}

	igt_subtest("invalid-wait-bad-flags")
		test_wait_bad_flags(fd);

	igt_subtest("invalid-wait-zero-handles")
		test_wait_zero_handles(fd);

	igt_subtest("invalid-wait-illegal-handle")
		test_wait_illegal_handle(fd);

	igt_subtest("invalid-query-zero-handles")
		test_query_zero_handles(fd);

	igt_subtest("invalid-query-illegal-handle")
		test_query_illegal_handle(fd);

	igt_subtest("invalid-query-one-illegal-handle")
		test_query_one_illegal_handle(fd);

	igt_subtest("invalid-query-bad-pad")
		test_query_bad_pad(fd);

	igt_subtest("invalid-signal-zero-handles")
		test_signal_zero_handles(fd);

	igt_subtest("invalid-signal-illegal-handle")
		test_signal_illegal_handle(fd);

	igt_subtest("invalid-signal-illegal-point")
		test_signal_illegal_point(fd);

	igt_subtest("invalid-signal-one-illegal-handle")
		test_signal_one_illegal_handle(fd);

	igt_subtest("invalid-signal-bad-pad")
		test_signal_bad_pad(fd);

	igt_subtest("invalid-signal-array")
		test_signal_array(fd);

	igt_subtest("invalid-transfer-illegal-handle")
		test_transfer_illegal_handle(fd);

	igt_subtest("invalid-transfer-bad-pad")
		test_transfer_bad_pad(fd);

	for (unsigned flags = 0; flags < WAIT_FLAGS_MAX; flags++) {
		int err;

		/* Only one wait mode for single-wait tests */
		if (__builtin_popcount(flags & (WAIT_UNSUBMITTED |
						WAIT_SUBMITTED |
						WAIT_SIGNALED)) != 1)
			continue;

		if ((flags & WAIT_UNSUBMITTED) && !(flags & WAIT_FOR_SUBMIT))
			err = -EINVAL;
		else if (!(flags & WAIT_SIGNALED) && !((flags & WAIT_SUBMITTED) && (flags & WAIT_AVAILABLE)))
			err = -ETIME;
		else
			err = 0;

		igt_subtest_f("%ssingle-wait%s%s%s%s%s%s",
			      err == -EINVAL ? "invalid-" : err == -ETIME ? "etime-" : "",
			      (flags & WAIT_ALL) ? "-all" : "",
			      (flags & WAIT_FOR_SUBMIT) ? "-for-submit" : "",
			      (flags & WAIT_AVAILABLE) ? "-available" : "",
			      (flags & WAIT_UNSUBMITTED) ? "-unsubmitted" : "",
			      (flags & WAIT_SUBMITTED) ? "-submitted" : "",
			      (flags & WAIT_SIGNALED) ? "-signaled" : "")
			test_single_wait(fd, flags, err);
	}

	igt_subtest("wait-delayed-signal")
		test_wait_delayed_signal(fd, 0);

	igt_subtest("wait-for-submit-delayed-submit")
		test_wait_delayed_signal(fd, WAIT_FOR_SUBMIT);

	igt_subtest("wait-all-delayed-signal")
		test_wait_delayed_signal(fd, WAIT_ALL);

	igt_subtest("wait-all-for-submit-delayed-submit")
		test_wait_delayed_signal(fd, WAIT_ALL | WAIT_FOR_SUBMIT);

	igt_subtest("reset-unsignaled")
		test_reset_unsignaled(fd);

	igt_subtest("reset-signaled")
		test_reset_signaled(fd);

	igt_subtest("reset-multiple-signaled")
		test_reset_multiple_signaled(fd);

	igt_subtest("reset-during-wait-for-submit")
		test_reset_during_wait_for_submit(fd);

	igt_subtest("signal")
		test_signal(fd);

	for (unsigned flags = 0; flags < WAIT_FLAGS_MAX; flags++) {
		int err;

		/* At least one wait mode for multi-wait tests */
		if (!(flags & (WAIT_UNSUBMITTED |
			       WAIT_SUBMITTED |
			       WAIT_SIGNALED)))
			continue;

		err = 0;
		if ((flags & WAIT_UNSUBMITTED) && !(flags & WAIT_FOR_SUBMIT)) {
			err = -EINVAL;
		} else if (flags & WAIT_ALL) {
			if (flags & (WAIT_UNSUBMITTED | WAIT_SUBMITTED))
				err = -ETIME;
			if (!(flags & WAIT_UNSUBMITTED) && (flags & WAIT_SUBMITTED) && (flags & WAIT_AVAILABLE))
				err = 0;
		} else {
			if (!(flags & WAIT_SIGNALED) && !((flags & WAIT_SUBMITTED) && (flags & WAIT_AVAILABLE)))
				err = -ETIME;
		}

		igt_subtest_f("%smulti-wait%s%s%s%s%s%s",
			      err == -EINVAL ? "invalid-" : err == -ETIME ? "etime-" : "",
			      (flags & WAIT_ALL) ? "-all" : "",
			      (flags & WAIT_FOR_SUBMIT) ? "-for-submit" : "",
			      (flags & WAIT_AVAILABLE) ? "-available" : "",
			      (flags & WAIT_UNSUBMITTED) ? "-unsubmitted" : "",
			      (flags & WAIT_SUBMITTED) ? "-submitted" : "",
			      (flags & WAIT_SIGNALED) ? "-signaled" : "")
			test_multi_wait(fd, flags, err);
	}
	igt_subtest("wait-any-snapshot")
		test_wait_snapshot(fd, 0);

	igt_subtest("wait-all-snapshot")
		test_wait_snapshot(fd, WAIT_ALL);

	igt_subtest("wait-for-submit-snapshot")
		test_wait_snapshot(fd, WAIT_FOR_SUBMIT);

	igt_subtest("wait-all-for-submit-snapshot")
		test_wait_snapshot(fd, WAIT_ALL | WAIT_FOR_SUBMIT);

	igt_subtest("wait-any-complex")
		test_wait_complex(fd, 0);

	igt_subtest("wait-all-complex")
		test_wait_complex(fd, WAIT_ALL);

	igt_subtest("wait-for-submit-complex")
		test_wait_complex(fd, WAIT_FOR_SUBMIT);

	igt_subtest("wait-all-for-submit-complex")
		test_wait_complex(fd, WAIT_ALL | WAIT_FOR_SUBMIT);

	igt_subtest("wait-any-interrupted")
		test_wait_interrupted(fd, 0);

	igt_subtest("wait-all-interrupted")
		test_wait_interrupted(fd, WAIT_ALL);

	igt_subtest("host-signal-points")
		test_host_signal_points(fd);

	igt_subtest("device-signal-unordered")
		test_device_signal_unordered(fd);

	igt_subtest("device-submit-unordered")
		test_device_submit_unordered(fd);

	igt_subtest("host-signal-ordered")
		test_host_signal_ordered(fd);

	igt_subtest("host-signal-unordered")
		test_host_signal_unordered(fd);
}
