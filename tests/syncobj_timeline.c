/*
 * Copyright © 2019 Intel Corporation
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
#include "igt_syncobj.h"
#include "sw_sync.h"
#include <sys/ioctl.h>

IGT_TEST_DESCRIPTION("Checks for timeline drm sync objects.");

/* Creating a signaled timeline semaphore is nonsense. */
static void
test_bad_create_signaled(int fd)
{
	struct drm_syncobj_create create = { 0 };
	int ret;

	create.flags = DRM_SYNCOBJ_CREATE_SIGNALED | DRM_SYNCOBJ_CREATE_TIMELINE;
	ret = igt_ioctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &create);
	igt_assert(ret == -1 && errno == EINVAL);
}

/* Signal a timeline syncobj without a point is invalid. */
static void
test_bad_timeline_signal(int fd)
{
	uint32_t syncobj = syncobj_create(fd, DRM_SYNCOBJ_CREATE_TIMELINE);
	struct drm_syncobj_array array = {
		.handles = to_user_pointer(&syncobj),
		.count_handles = 1,
	};
	int ret;

	ret = igt_ioctl(fd, DRM_IOCTL_SYNCOBJ_SIGNAL, &array);
	igt_assert(ret == -1 && errno == EINVAL);

	syncobj_destroy(fd, syncobj);
}

/*
 * Reading the value of a timeline syncobj require a special flag in
 * the drm_syncobj_array.flags field.
 */
static void
test_bad_timeline_read(int fd)
{
	uint32_t syncobj = syncobj_create(fd, DRM_SYNCOBJ_CREATE_TIMELINE);
	struct drm_syncobj_item sig = {
		.handle = syncobj, .value = 10,
	};
	struct drm_syncobj_array array = {
		.handles = to_user_pointer(&sig),
		.count_handles = 1,
	};
	int ret;

	syncobj_signal2(fd, &sig, 1);

	array.flags = 0;
	ret = igt_ioctl(fd, DRM_IOCTL_SYNCOBJ_READ_TIMELINE, &array);
	igt_assert(ret == -1 && errno == EINVAL);

	array.flags = 0xdeadbeef;
	ret = igt_ioctl(fd, DRM_IOCTL_SYNCOBJ_READ_TIMELINE, &array);
	igt_assert(ret == -1 && errno == EINVAL);

	syncobj_destroy(fd, syncobj);
}

/* Reading a binary syncobj's value doesn't make sense. */
static void
test_bad_binary_read(int fd)
{
	uint32_t syncobj = syncobj_create(fd, 0);
	struct drm_syncobj_item sig = {
		.handle = syncobj, .value = 10,
	};
	struct drm_syncobj_array array = {
		.handles = to_user_pointer(&sig),
		.count_handles = 1,
	};
	int ret;

	syncobj_signal(fd, &syncobj, 1);

	array.flags = 0;
	ret = igt_ioctl(fd, DRM_IOCTL_SYNCOBJ_READ_TIMELINE, &array);
	igt_assert(ret == -1 && errno == EINVAL);

	syncobj_destroy(fd, syncobj);
}

/*
 * Resetting a timeline syncobj require a point and that is given
 * using a special flag in the drm_syncobj_array.flags field.
 *
 * Trying to reset point 0 is also invalid on a timeline syncobj.
 */
static void
test_bad_timeline_reset(int fd)
{
	uint32_t syncobj = syncobj_create(fd, DRM_SYNCOBJ_CREATE_TIMELINE);
	struct drm_syncobj_array array = {
		.handles = to_user_pointer(&syncobj),
		.count_handles = 1,
	};
	struct drm_syncobj_item item = {
		.handle = syncobj,
		.value = 0, /* invalid for timeline syncobj */
	};
	struct drm_syncobj_array array_item = {
		.handles = to_user_pointer(&item),
		.count_handles = 1,
	};
	int ret;

	syncobj_signal_point(fd, syncobj, 10);

	array.flags = 0;
	ret = igt_ioctl(fd, DRM_IOCTL_SYNCOBJ_RESET, &array);
	igt_assert(ret == -1 && errno == EINVAL);

	array.flags = 0xdeadbeef;
	ret = igt_ioctl(fd, DRM_IOCTL_SYNCOBJ_RESET, &array);
	igt_assert(ret == -1 && errno == EINVAL);

	array.flags = DRM_SYNCOBJ_ARRAY_FLAGS_ITEMS;
	ret = igt_ioctl(fd, DRM_IOCTL_SYNCOBJ_RESET, &array_item);
	igt_assert(ret == -1 && errno == EINVAL);

	igt_assert_eq(syncobj_read_timeline(fd, syncobj), 10);

	syncobj_destroy(fd, syncobj);
}

/*
 * Trying to reset a point different from 0 on a binary syncobj is
 * invalid.
 */
static void
test_bad_binary_reset(int fd)
{
	uint32_t syncobj = syncobj_create(fd, 0);
	struct drm_syncobj_item item = {
		.handle = syncobj,
		.value = 42, /* invalid for binary syncobj */
	};
	struct drm_syncobj_array array_item = {
		.handles = to_user_pointer(&item),
		.count_handles = 1,
	};
	int ret;

	syncobj_signal(fd, &syncobj, 1);

	array_item.flags = DRM_SYNCOBJ_ARRAY_FLAGS_ITEMS;
	ret = igt_ioctl(fd, DRM_IOCTL_SYNCOBJ_RESET, &array_item);
	igt_assert(ret == -1 && errno == EINVAL);

	syncobj_destroy(fd, syncobj);
}

/* Reset a signaled point doesn't do anything. */
static void
test_reset_signaled(int fd)
{
	uint32_t syncobj = syncobj_create(fd, DRM_SYNCOBJ_CREATE_TIMELINE);
	struct drm_syncobj_item item = {
		.handle = syncobj,
		.value = 10,
	};

	syncobj_signal_point(fd, syncobj, 40);
	syncobj_reset2(fd, &item, 1);

	igt_assert_eq(syncobj_read_timeline(fd, syncobj), 40);

	syncobj_destroy(fd, syncobj);
}

/*
 * Signaling any point never errors out. Verify that the timeline
 * object value is monotonically incremented.
 */
static void
test_increment_monotonically(int fd)
{
	static const uint64_t random_points[] = {
		9000, 42, 13, 5000, 100000, 1201240129, 3, 7 };
	uint64_t timeline_value = 0;
	uint32_t syncobj = syncobj_create(fd, DRM_SYNCOBJ_CREATE_TIMELINE);
	uint32_t i;

	igt_assert_eq(syncobj_read_timeline(fd, syncobj), 0);

	for (i = 0; i < ARRAY_SIZE(random_points); i++) {
		igt_debug("Signaling point %lu\n", random_points[i]);

		syncobj_signal_point(fd, syncobj, random_points[i]);

		timeline_value = max(timeline_value, random_points[i]);

		igt_assert_eq(syncobj_read_timeline(fd, syncobj), timeline_value);
	}

	syncobj_destroy(fd, syncobj);
}

/*
 * Verify the ordering of signaling and how waiters should be waken
 * up.
 */
static void
test_signal_order(int fd)
{
	uint32_t syncobj = syncobj_create(fd, DRM_SYNCOBJ_CREATE_TIMELINE);
	struct drm_syncobj_item sig = {
		.handle = syncobj,
	};
	struct drm_syncobj_item waits[] = {
		{ .handle = syncobj, .value = 20, },
		{ .handle = syncobj, .value = 15, },
	};
	uint32_t first = 0xdeadbeef;

	/* Move the timeline to 10. */
	sig.value = 10;
	syncobj_signal2(fd, &sig, 1);

	/* Poll 2 points 15 & 20 which should not be signaled. */
	igt_assert(!syncobj_wait2(fd, waits, ARRAY_SIZE(waits), 0,
				  DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL));

	/* Advance timeline to 12. */
	sig.value = 17;
	syncobj_signal2(fd, &sig, 1);

	/* Poll again on both 15 & 20, still not all signaled. */
	igt_assert(!syncobj_wait2(fd, waits, ARRAY_SIZE(waits), 0,
				  DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
				  DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
				  NULL));
	/* Poll at least one of 15 & 20. */
	igt_assert(syncobj_wait2(fd, waits, ARRAY_SIZE(waits), 0,
				 DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
				 &first));
	/* Point 15 in waits[1] is raised as signaled. */
	igt_assert_eq(first, 1);

	/* Advance timeline to 20. */
	sig.value = 20;
	syncobj_signal2(fd, &sig, 1);
	igt_assert(syncobj_wait2(fd, waits, ARRAY_SIZE(waits), 0,
				 DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL |
				 DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
				 NULL));

	syncobj_destroy(fd, syncobj);
}

static uint64_t
get_timestamp(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static bool wait_for_value(const uint32_t *observed, uint32_t expected, uint64_t timeout_ns)
{
	uint64_t ts0 = get_timestamp();
	uint64_t ts1 = ts0;

	while ((ts1 - ts0) < timeout_ns) {
		ts1 = get_timestamp();
	}

	igt_debug("waiting observed/expected=%u/%u...\n", *observed, expected);

	return *observed == expected;
}

/*
 * Verify that replacing a point on which a process is waiting is
 * wakeup the waiting appropriately depending on whether the
 * replacement signals the point.
 */
static void
test_reset_past_point_during_wait_for_submit(int fd,
					     uint64_t wait_point,
					     uint64_t replace_point,
					     bool replace_signaled)
{
	struct {
		uint32_t processes_started;
		uint32_t processes_signaled;
		uint64_t wait_value;
		uint32_t waiting;

	} *shared_map = mmap(0, 4096, PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
	uint32_t syncobj = syncobj_create(fd, DRM_SYNCOBJ_CREATE_TIMELINE);
	int timeline1 = sw_sync_timeline_create(), timeline2 = sw_sync_timeline_create();
	int fence;

	/* First put a fence into the point we're about to wait on. */
	fence = sw_sync_timeline_create_fence(timeline1, 1);
	syncobj_import_sync_file2(fd, syncobj, replace_point, fence);
	close(fence);

	igt_fork(child, 1) {
		struct drm_syncobj_item item = {
			.handle = syncobj, .value = wait_point,
		};

		shared_map->waiting = true;

		syncobj_wait2(fd, &item, 1, INT64_MAX,
			      DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
			      NULL);

		shared_map->waiting = false;
	}

	/* Wait for the forked process to wait. */
	igt_assert(wait_for_value(&shared_map->waiting, true, 10000000 /* 10ms */));

	/* Now replace the waited point's fence with another fence. */
	fence = sw_sync_timeline_create_fence(timeline2, 1);
	if (replace_signaled)
		sw_sync_timeline_inc(timeline2, 1);
	syncobj_import_sync_file2(fd, syncobj, replace_point, fence);
	close(fence);

	if (replace_point >= wait_point && replace_signaled)
		igt_assert(wait_for_value(&shared_map->waiting, false, 10000000 /* 10ms */));
	else
		igt_assert(wait_for_value(&shared_map->waiting, true, 10000000 /* 10ms */));

	syncobj_signal_point(fd, syncobj, wait_point);

	munmap(shared_map, 4096);

	close(timeline1);
	close(timeline2);

	syncobj_destroy(fd, syncobj);
}

/*
 * Verifies that cpu waits are awaken in proper order by cpu signals
 * from other processes.
 */
static void
test_cpu_cpu_signaling(int fd)
{
	struct {
		uint32_t processes_started;
		uint32_t processes_signaled;
		struct {
			uint64_t wait_value;
			uint64_t wakeup_time;
		} processes[20];
	} *shared_map = mmap(0, 4096, PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
	uint32_t syncobj = syncobj_create(fd, DRM_SYNCOBJ_CREATE_TIMELINE);
	int syncobj_fd = syncobj_handle_to_fd(fd, syncobj, 0);
	uint64_t signaling_start_ts, signaling_end_ts;
	uint64_t value = 0;
	uint32_t i;

	for (i = 0; i < ARRAY_SIZE(shared_map->processes); i++) {
		value += rand() % 100;
		shared_map->processes[i].wait_value = value;
		igt_debug("process%02u waiting on point%05lu\n", i,
			  shared_map->processes[i].wait_value);
	}

	for (i = 0; i < ARRAY_SIZE(shared_map->processes); i++) {
		igt_fork(child, 1) {
			struct drm_syncobj_item item = {
				.value = shared_map->processes[i].wait_value
			};

			fd = drm_open_driver(DRIVER_ANY);
			item.handle = syncobj_fd_to_handle(fd, syncobj_fd, 0);

			__sync_fetch_and_add(&shared_map->processes_started, 1);

			syncobj_wait2(fd, &item, 1, INT64_MAX,
				      DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
				      NULL);

			shared_map->processes[i].wakeup_time = get_timestamp();

			__sync_fetch_and_add(&shared_map->processes_signaled, 1);

			syncobj_destroy(fd, item.handle);
		}
	}

	/* Wait for all the child processes to wait on the timeline
	 * semaphore.
	 */
	igt_debug("waiting for processes to call into the wait primitive...\n");
	igt_assert(wait_for_value(&shared_map->processes_started,
				  ARRAY_SIZE(shared_map->processes),
				  10000000 /* 10ms */));
	igt_assert_eq(shared_map->processes_signaled, 0);

	/* Signal everybody one by one. */
	signaling_start_ts = get_timestamp();

	for (i = 0; i < ARRAY_SIZE(shared_map->processes); i++) {
		struct drm_syncobj_item item = {
			.handle = syncobj,
			.value = shared_map->processes[i].wait_value,
		};

		syncobj_signal2(fd, &item, 1);
		/* Leave some time between each process signaled. */
		usleep(1000 /* 1ms */);
	}

	igt_debug("waiting for processes to wake up...\n");
	igt_assert(wait_for_value(&shared_map->processes_signaled,
				  ARRAY_SIZE(shared_map->processes),
				  10000000 /* 10ms */));

	signaling_end_ts = get_timestamp();

	igt_debug("start=0x%016lx end=0x%016lx\n",
		  signaling_start_ts, signaling_end_ts);
	for (i = 0; i < ARRAY_SIZE(shared_map->processes); i++) {
		igt_debug("process%02i waiting on point%05lu wakeup at 0x%016lx\n", i,
			  shared_map->processes[i].wait_value,
			  shared_map->processes[i].wakeup_time);
		if (i > 0) {
			igt_assert(shared_map->processes[i - 1].wakeup_time <=
				   shared_map->processes[i].wakeup_time);
		}
		igt_assert(shared_map->processes[i].wakeup_time >= signaling_start_ts);
		igt_assert(shared_map->processes[i].wakeup_time <= signaling_end_ts);
	}

	munmap(shared_map, 4096);

	syncobj_destroy(fd, syncobj);
}

/*
 * Verifies that cpu waits are awaken in proper order by device
 * signals from other processes.
 */
static void
test_device_cpu_signaling(int fd)
{
	struct {
		uint32_t processes_started;
		uint32_t processes_signaled;
		struct {
			uint64_t wait_value;
			uint64_t wakeup_time;
		} processes[20];
	} *shared_map = mmap(0, 4096, PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
	uint32_t syncobj = syncobj_create(fd, DRM_SYNCOBJ_CREATE_TIMELINE);
	int timeline = sw_sync_timeline_create();
	int syncobj_fd = syncobj_handle_to_fd(fd, syncobj, 0);
	uint64_t signaling_start_ts, signaling_end_ts;
	uint64_t value = 0;
	uint32_t i;

	for (i = 0; i < ARRAY_SIZE(shared_map->processes); i++) {
		value += rand() % 100;
		shared_map->processes[i].wait_value = value;
		igt_debug("process%02u waiting on point%05lu\n", i,
			  shared_map->processes[i].wait_value);
	}

	for (i = 0; i < ARRAY_SIZE(shared_map->processes); i++) {
		int fence;

		/*
		 * Import a fence from the sw timeline into the
		 * syncobj timeline point we're about to wait on.
		 */
		fence = sw_sync_timeline_create_fence(timeline, i + 1);
		syncobj_import_sync_file2(fd, syncobj,
					  shared_map->processes[i].wait_value,
					  fence);
		close(fence);

		igt_fork(child, 1) {
			struct drm_syncobj_item item = {
				.value = shared_map->processes[i].wait_value
			};

			fd = drm_open_driver(DRIVER_ANY);
			item.handle = syncobj_fd_to_handle(fd, syncobj_fd, 0);

			__sync_fetch_and_add(&shared_map->processes_started, 1);

			syncobj_wait2(fd, &item, 1, INT64_MAX,
				      DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT,
				      NULL);

			shared_map->processes[i].wakeup_time = get_timestamp();

			__sync_fetch_and_add(&shared_map->processes_signaled, 1);

			syncobj_destroy(fd, item.handle);
		}
	}

	/* Wait for all the child processes to wait on the timeline
	 * semaphore.
	 */
	igt_debug("waiting for processes to call into the wait primitive...\n");
	igt_assert(wait_for_value(&shared_map->processes_started,
				  ARRAY_SIZE(shared_map->processes),
				  100000000 /* 100ms */));
	igt_assert_eq(shared_map->processes_signaled, 0);

	/* Signal everybody one by one. */
	signaling_start_ts = get_timestamp();

	for (i = 0; i < ARRAY_SIZE(shared_map->processes); i++) {
		sw_sync_timeline_inc(timeline, 1);
		/* Leave some time between each process signaled. */
		usleep(1000 /* 1ms */);
	}

	igt_debug("waiting for processes to wake up...\n");
	igt_assert(wait_for_value(&shared_map->processes_signaled,
				  ARRAY_SIZE(shared_map->processes),
				  100000000 /* 100ms */));

	signaling_end_ts = get_timestamp();

	igt_debug("start=0x%016lx end=0x%016lx\n",
		  signaling_start_ts, signaling_end_ts);
	for (i = 0; i < ARRAY_SIZE(shared_map->processes); i++) {
		igt_debug("process%02i waiting on point%05lu wakeup at 0x%016lx\n", i,
			  shared_map->processes[i].wait_value,
			  shared_map->processes[i].wakeup_time);
		if (i > 0) {
			igt_assert(shared_map->processes[i - 1].wakeup_time <=
				   shared_map->processes[i].wakeup_time);
		}
		igt_assert(shared_map->processes[i].wakeup_time >= signaling_start_ts);
		igt_assert(shared_map->processes[i].wakeup_time <= signaling_end_ts);
	}

	munmap(shared_map, 4096);

	close(timeline);

	syncobj_destroy(fd, syncobj);
}

static bool has_syncobj_timeline(int fd)
{
	uint64_t value;
	if (drmGetCap(fd, DRM_CAP_SYNCOBJ_TIMELINE, &value))
		return false;
	return value ? true : false;
}

igt_main
{
	int fd = -1;

	igt_fixture {
		fd = drm_open_driver(DRIVER_ANY);
		igt_require(has_syncobj_timeline(fd));
	}

	igt_subtest("bad-create-signaled")
		test_bad_create_signaled(fd);

	igt_subtest("bad-timeline-signal")
		test_bad_timeline_signal(fd);

	igt_subtest("bad-timeline-read")
		test_bad_timeline_read(fd);

	igt_subtest("bad-binary-read")
		test_bad_binary_read(fd);

	igt_subtest("bad-timeline-reset")
		test_bad_timeline_reset(fd);

	igt_subtest("bad-binary-reset")
		test_bad_binary_reset(fd);

	igt_subtest("reset-signaled")
		test_reset_signaled(fd);

	igt_subtest("increment-monotonically")
		test_increment_monotonically(fd);

	igt_subtest("signal-order")
		test_signal_order(fd);

	{
		struct {
			const char *name;
			uint64_t wait;
			uint64_t replace;
			bool signaled_replace;
		} subtests[] = {
			{ "signaled-past", 50, 30, true },
			{ "signaled-future", 30, 50, true },
			{ "unsignaled-past", 50, 30, false },
			{ "unsignaled-future", 30, 50, false },
		};
		uint32_t i;

		for (i = 0; i < ARRAY_SIZE(subtests); i++) {
			igt_subtest_f("reset-%s-during-wait-for-submit", subtests[i].name) {
				test_reset_past_point_during_wait_for_submit(
					fd, subtests[i].wait, subtests[i].replace,
					subtests[i].signaled_replace);
			}
		}
	}

	igt_subtest("multi-process-cpu-cpu-signaling")
		test_cpu_cpu_signaling(fd);

	igt_subtest("multi-process-device-cpu-signaling")
		test_device_cpu_signaling(fd);
}
