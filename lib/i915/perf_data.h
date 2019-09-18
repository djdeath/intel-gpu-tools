/*
 * Copyright (C) 2019 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef PERF_DATA_H
#define PERF_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

/* The structures below are embedded in the i915-perf stream so as to
 * provide metadata. The types used in the
 * drm_i915_perf_record_header.type are defined in
 * intel_perf_record_type.
 *
 * Once defined, those structures cannot change. If you need to add
 * new data, just define a new structure & record_type.
 */

#include <stdint.h>

enum intel_perf_record_type {
	/* Start at 65536, which is pretty safe since after 3years the
	 * kernel hasn't defined more than 3 entries.
	 */

	/* intel_perf_record_device_info */
	INTEL_PERF_RECORD_TYPE_DEVICE_INFO = 1 << 16,

	/* intel_perf_record_device_topology */
	INTEL_PERF_RECORD_TYPE_DEVICE_TOPOLOGY,

	/* intel_perf_record_timestamp_correlation */
	INTEL_PERF_RECORD_TYPE_TIMESTAMP_CORRELATION,
};

struct intel_perf_record_device_info {
	/* Frequency of the timestamps in the records. */
	uint64_t timestamp_frequency;

	/* PCI ID */
	uint32_t device_id;

	/* enum drm_i915_oa_format */
	uint32_t oa_format;

	/* Configuration identifier */
	char uuid[40];
};

/* Topology as reported by i915. */
struct intel_perf_record_device_topology {
	struct drm_i915_query_topology_info topology;
};

/* Timestamp correlation between CPU/GPU. */
struct intel_perf_record_timestamp_correlation {
	/* In CLOCK_MONOTONIC */
	uint64_t cpu_timestamp;

	/* Engine timestamp associated with the OA unit */
	uint64_t gpu_timestamp;
};

#ifdef __cplusplus
};
#endif

#endif /* PERF_DATA_H */
