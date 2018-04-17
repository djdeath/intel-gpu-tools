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
 *
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <errno.h>
#include <values.h>
#include <sys/ioctl.h>
#include <pthread.h>

#include "drm.h"
#include "ioctl_wrappers.h"
#include "drmtest.h"
#include "intel_batchbuffer.h"
#include "intel_chipset.h"
#include "intel_io.h"
#include "intel_reg.h"
#include "igt_aux.h"
#include "igt_sysfs.h"
#include "igt_stats.h"

#define MI_LOAD_REGISTER_MEM  (0x29 << 23)
#define MI_STORE_REGISTER_MEM (0x24 << 23)

#define CS_GPR(n)     (0x2600 + (n) * 8)
#define RCS_TIMESTAMP (0x2000 + 0x358)

#define OAREPORT_REASON_MASK           0x3f
#define OAREPORT_REASON_SHIFT          19
#define OAREPORT_REASON_TIMER          (1<<0)
#define OAREPORT_REASON_INTERNAL       (3<<1)
#define OAREPORT_REASON_CTX_SWITCH     (1<<3)
#define OAREPORT_REASON_GO             (1<<4)
#define OAREPORT_REASON_CLK_RATIO      (1<<5)

static uint64_t timestamp_frequency = 0;

static uint64_t
timebase_scale(uint32_t u32_delta)
{
	return ((uint64_t)u32_delta * NSEC_PER_SEC) / timestamp_frequency;
}

struct human_scale_unit {
	double value;
	const char *unit;
};

static struct human_scale_unit
human(double value)
{
	static const char *time_units[] = { "ns", "us", "ms", "s" };
	struct human_scale_unit result;
	int i = 0;

	while (i < ARRAY_SIZE(time_units) && (value / 1000.0f) >= 1.0f) {
		value /= 1000.0f;
		i++;
	}

	result.value = value;
	result.unit = time_units[min(i, ARRAY_SIZE(time_units) - 1)];

	return result;
}

static int
max_oa_exponent_for_period_lte(uint64_t ns_period)
{
	int i;

	/* NB: timebase_scale() takes a uint32_t and an exponent of 30
	 * would already represent a period of ~3 minutes so there's
	 * really no need to consider higher exponents.
	 */
	for (i = 0; i < 30; i++) {
		uint64_t oa_period = timebase_scale(2 << i);

		if (oa_period > ns_period)
			return max(0, i - 1);
	}

	return 29;
}

static uint64_t
get_timestamp_frequency(int fd)
{
	int cs_ts_freq = 0;
	drm_i915_getparam_t gp;

	gp.param = I915_PARAM_CS_TIMESTAMP_FREQUENCY;
	gp.value = &cs_ts_freq;
	if (igt_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp) == 0)
		return cs_ts_freq;

	igt_assert(!"unable to query timestamp frequency");

	return 0;
}

static int
__perf_open(int fd)
{
	uint64_t properties[] = {
		/* Include OA reports in samples */
		DRM_I915_PERF_PROP_SAMPLE_OA, true,

		/* OA unit configuration */
		DRM_I915_PERF_PROP_OA_METRICS_SET, 1, /* test config */
		DRM_I915_PERF_PROP_OA_FORMAT, I915_OA_FORMAT_A32u40_A4u32_B8_C8,
		DRM_I915_PERF_PROP_OA_EXPONENT,
		max_oa_exponent_for_period_lte(60 * 1000000000ULL /* 1 minute */),
	};
	struct drm_i915_perf_open_param param = {
		.flags = I915_PERF_FLAG_FD_CLOEXEC |
		I915_PERF_FLAG_FD_NONBLOCK,
		.num_properties = ARRAY_SIZE(properties) / 2,
		.properties_ptr = to_user_pointer(properties),
	};
	int ret;

	ret = igt_ioctl(fd, DRM_IOCTL_I915_PERF_OPEN, &param);

	igt_assert(ret >= 0);

	return ret;
}

struct perf_data {
	struct perf_data *next;

	int used_size;
	uint8_t data[16 * 1024 * 1024];
};

static bool perf_ready = false, perf_shutdown = false, perf_done = false;
static uint32_t perf_end;
static struct perf_data *perf_data = NULL;

static uint32_t last_ts(struct perf_data *data)
{
	struct drm_i915_perf_record_header *header =
		(struct drm_i915_perf_record_header *) data->data;
	uint32_t ts = 0;

	while (((uint8_t *) header < (data->data + data->used_size))) {
		switch (header->type) {
		case DRM_I915_PERF_RECORD_OA_REPORT_LOST:
			igt_debug("report loss\n");
			break;
		case DRM_I915_PERF_RECORD_OA_BUFFER_LOST:
			igt_assert(!"unexpected overflow");
			break;
		case DRM_I915_PERF_RECORD_SAMPLE: {
			uint32_t *report = (uint32_t *) (header + 1);

			ts = report[1];
			break;
		}
		}

		header = (struct drm_i915_perf_record_header *) (((uint8_t *) header) +
								 header->size);
	}

	return ts;
}

static void *perf_reader(void *ptr)
{
	int fd = (int) (uintptr_t) ptr;
	int perf_fd = __perf_open(fd);
	struct perf_data *data;

	perf_data = data = calloc(1, sizeof(*perf_data));

	perf_ready = true;

	while (last_ts(data) < perf_end) {
		while (last_ts(data) < perf_end) {
			int ret = read(perf_fd, &data->data[data->used_size],
				       sizeof(data->data) - data->used_size);

			if (ret == -1) {
				switch (errno) {
				case ENOSPC: {
					struct perf_data *old_data = data;
					data = calloc(1, sizeof(*perf_data));
					old_data->next = data;
					break;
				}
				case EINTR:
				case EAGAIN:
					break;
				default:
					igt_assert(!"error");
				}
			} else {
				data->used_size += ret;
				break;
			}
		}
	}

	close(perf_fd);
	perf_done = true;

	return NULL;
}

static void batch(int fd,
		  int devid,
		  drm_intel_bufmgr *bufmgr,
		  drm_intel_context *context,
		  drm_intel_bo *dst_bo,
		  int ts_dest)
{
	struct intel_batchbuffer *batch;

	batch = intel_batchbuffer_alloc(bufmgr, devid);
	igt_assert(batch);

	intel_batchbuffer_set_context(batch, context);

	BEGIN_BATCH(3 * 2 + 1, 2);

	OUT_BATCH(MI_STORE_REGISTER_MEM | (4 - 2));
	OUT_BATCH(RCS_TIMESTAMP);
	OUT_RELOC_FENCED(dst_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, ts_dest);

	OUT_BATCH(MI_LOAD_REGISTER_MEM | (4 - 2));
	OUT_BATCH(CS_GPR(0));
	OUT_RELOC_FENCED(dst_bo, I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER, ts_dest);

	OUT_BATCH(MI_BATCH_BUFFER_END);

	ADVANCE_BATCH();

	intel_batchbuffer_flush_on_ring(batch, I915_EXEC_RENDER);

	intel_batchbuffer_free(batch);
}

static bool
is_ctx_switch_report(const uint32_t *report)
{
	uint32_t reason = ((report[0] >> OAREPORT_REASON_SHIFT) &
			   OAREPORT_REASON_MASK);

	return (reason & OAREPORT_REASON_CTX_SWITCH) != 0;
}

static uint64_t
measure_delay(uint32_t *timestamps, int n_timestamps)
{
	struct perf_data *data = perf_data;
	struct drm_i915_perf_record_header *header =
		(struct drm_i915_perf_record_header *) data->data;
	uint64_t *deltas = calloc(n_timestamps, sizeof(uint64_t));
	uint32_t last_oa_ts = 0;
	uint32_t next_oa_ts = last_oa_ts;
	uint32_t last_ctx_id = 0xffffffff;
	double average, dmin, dmax, variance, std_deviation, sum;
	int i = 0;

	while (i < n_timestamps && data) {
		while (i < n_timestamps &&
		       ((uint8_t *) header < (data->data + data->used_size))) {

			switch (header->type) {
			case DRM_I915_PERF_RECORD_OA_REPORT_LOST:
				igt_debug("report loss\n");
				break;
			case DRM_I915_PERF_RECORD_OA_BUFFER_LOST:
				igt_assert(!"unexpected overflow");
				break;
			case DRM_I915_PERF_RECORD_SAMPLE: {
				uint32_t *report = (uint32_t *) (header + 1);

				if (!is_ctx_switch_report(report))
					break;

				if (last_ctx_id == report[2])
					break;

				last_oa_ts = next_oa_ts;
				next_oa_ts = report[1];
				last_ctx_id = report[2];

				if (timestamps[i] > last_oa_ts && timestamps[i] < next_oa_ts) {
					deltas[i] = timebase_scale(timestamps[i] - last_oa_ts);
					i++;
				}

				break;
			}
			}

			header = (struct drm_i915_perf_record_header *) (((uint8_t *) header) +
									 header->size);
		}

		data = data->next;
		header = (struct drm_i915_perf_record_header *) data->data;
	}

	if (i < n_timestamps) {
		deltas[i] = timebase_scale(timestamps[i] - last_oa_ts);
	}

	average = 0.0f;
	dmin = MAXFLOAT;
	dmax = MINFLOAT;
	for (i = 0; i < n_timestamps; i++) {
		dmin = min(dmin, deltas[i]);
		dmax = max(dmax, deltas[i]);
		average += deltas[i];
	}
	average /= n_timestamps;

	sum = 0;
	for (i = 0; i < n_timestamps; i++)
		sum += pow(deltas[i] - average, 2);
	variance = sum / n_timestamps;
	std_deviation = sqrt(variance);

	fprintf(stdout, "average=%.2f%s min/max=%.2f%s/%.2f%s variance=%.2f%s std_deviation=%.2f%s\n",
		human(average).value, human(average).unit,
		human(dmin).value, human(dmin).unit,
		human(dmax).value, human(dmax).unit,
		human(variance).value, human(variance).unit,
		human(std_deviation).value, human(std_deviation).unit);

	free(deltas);

	return 0;
}

enum mode {
	NONE,
	RPCS,
};

static void
context_set_slice_mask(int fd,
		       drm_intel_context *context,
		       uint64_t engine,
		       uint32_t slice_mask)
{
	struct drm_i915_gem_context_param arg;
	struct drm_i915_gem_context_param_sseu sseu;
	uint32_t context_id;
	int ret;

	memset(&sseu, 0, sizeof(sseu));
	sseu.class = 0; /* rcs */
	sseu.instance = 0;

	ret = drm_intel_gem_context_get_id(context, &context_id);
	igt_assert_eq(ret, 0);

	memset(&arg, 0, sizeof(arg));
	arg.ctx_id = context_id;
	arg.param = I915_CONTEXT_PARAM_SSEU;
	arg.value = (uintptr_t) &sseu;

	ret = igt_ioctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_GETPARAM, &arg);
	igt_assert_eq(ret, 0);

	sseu.slice_mask = slice_mask;

	ret = igt_ioctl(fd, DRM_IOCTL_I915_GEM_CONTEXT_SETPARAM, &arg);
	igt_assert_eq(ret, 0);
}

static int loop(int reps, enum mode mode, bool no_measurement)
{
	int fd = drm_open_driver(DRIVER_INTEL);
	int devid = intel_get_drm_devid(fd);
	int sysfs = igt_sysfs_open(fd, NULL);
	drm_intel_bufmgr *bufmgr = drm_intel_bufmgr_gem_init(fd, 4096);
	drm_intel_context *contexts[2], *initial_context;
	drm_intel_bo *dst_bo = drm_intel_bo_alloc(bufmgr, "target bo",
						  ALIGN(4 * reps + 1, 4096), 4096);
	pthread_t perf_thread;
	int i, ret, ts_loc = 0;
	uint32_t boost_freq, old_min_freq;

	if (intel_gen(devid) < 8) {
		fprintf(stderr, "Unavailable prior to Gen8\n");
		return 0;
	}

	boost_freq = igt_sysfs_get_u32(sysfs, "gt_RP0_freq_mhz");
	old_min_freq = igt_sysfs_get_u32(sysfs, "gt_min_freq_mhz");
	igt_sysfs_set_u32(sysfs, "gt_min_freq_mhz", boost_freq);

	initial_context = drm_intel_gem_context_create(bufmgr);
	contexts[0] = drm_intel_gem_context_create(bufmgr);
	contexts[1] = drm_intel_gem_context_create(bufmgr);

	switch (mode) {
	case NONE:
		/* No change */
		break;
	case RPCS:
		/* Smallest powergating configuration */
		context_set_slice_mask(fd, contexts[0], 0x1, 0x1);
		break;
	}

	timestamp_frequency = get_timestamp_frequency(fd);

	if (!no_measurement) {
		perf_end = 0xffffffff;
		pthread_create(&perf_thread, NULL, perf_reader, (void *) (uintptr_t) fd);
		while (!perf_ready);
	}

	batch(fd, devid, bufmgr, initial_context, dst_bo, 0);

	i = 0;
	while (i < reps) {
		batch(fd, devid, bufmgr, contexts[i % 2], dst_bo, ts_loc);
		ts_loc += 4;
		i++;
	}

	drm_intel_bo_wait_rendering(dst_bo);

	ret = drm_intel_bo_map(dst_bo, false /* write enable */);
	igt_assert_eq(ret, 0);

	perf_end = *((uint32_t *) (dst_bo->virtual + ts_loc - 4));

	if (!no_measurement) {
		perf_shutdown = true;
		while (!perf_done);
		pthread_join(perf_thread, NULL);
	}

	if (!no_measurement)
		measure_delay((uint32_t *) dst_bo->virtual, reps);

	drm_intel_bo_unmap(dst_bo);
	drm_intel_bo_unreference(dst_bo);
	close(fd);

	igt_sysfs_set_u32(sysfs, "gt_min_freq_mhz", old_min_freq);

	return 0;
}

int main(int argc, char **argv)
{
	enum mode mode = 0;
	int c, reps = 1;
	bool no_measurement = false;

	while ((c = getopt (argc, argv, "m:nr:")) != -1) {
		switch (c) {
		case 'm':
			if (strcmp(optarg, "none") == 0)
				mode = NONE;
			else if (strcmp(optarg, "rpcs") == 0)
				mode = RPCS;
			else {
				fprintf(stderr, "Invalid mode: %s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;

		case 'n':
			no_measurement = true;
			break;

		case 'r':
			reps = atoi(optarg);
			if (reps < 1)
				reps = 1;
			break;

		default:
			break;
		}
	}

	return loop(reps, mode, no_measurement);
}
