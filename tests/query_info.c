/*
 * Copyright © 2017 Intel Corporation
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

IGT_TEST_DESCRIPTION("Testing the engine info uAPI.");

#ifndef DRM_I915_QUERY_INFO
#define DRM_I915_QUERY_INFO		0x39
#define DRM_IOCTL_I915_QUERY_INFO	DRM_IOWR(DRM_COMMAND_BASE + DRM_I915_QUERY_INFO, struct local_drm_i915_query_info)
#endif

#ifndef I915_QUERY_INFO_ENGINE
#define I915_QUERY_INFO_ENGINE 0
#endif

enum local_drm_i915_engine_class {
	LOCAL_I915_ENGINE_CLASS_OTHER = 0,
	LOCAL_I915_ENGINE_CLASS_RENDER = 1,
	LOCAL_I915_ENGINE_CLASS_COPY = 2,
	LOCAL_I915_ENGINE_CLASS_VIDEO = 3,
	LOCAL_I915_ENGINE_CLASS_VIDEO_ENHANCE = 4,
	I915_ENGINE_CLASS_MAX /* non-ABI */
};

struct local_drm_i915_engine_info {
	/** Engine instance number. */
	__u8 instance;

	/** Engine specific info. */
#define I915_VCS_HAS_HEVC	BIT(0)
	__u8 info;

	__u8 rsvd[6];
};

struct local_drm_i915_query_info {
	/* in/out: Protocol version requested/supported. When set to 0, the
	 * kernel set this field to the current supported version. EINVAL will
	 * be return if version is greater than what the kernel supports.
	 */
	__u32 version;

	/* in: Query to perform on the engine (use one of
	 * I915_QUERY_INFO_* define).
	 */
	__u32 query;

	/** in: Parameters associated with the query (refer to each
	 * I915_QUERY_INFO_* define)
	 */
	__u32 query_params[3];

	/* in/out: Size of the data to be copied into info_ptr. When set to 0,
	 * the kernel set this field to the size to be copied into info_ptr.
	 * Call this one more time with the correct value set to make the
	 * kernel copy the data into info_ptr.
	 */
	__u32 info_ptr_len;

	/** in/out: Pointer to the data filled for the query (pointer set by
	 * the caller, data written by the callee).
	 */
	__u64 info_ptr;
};

static bool query_info_supports(int fd, int version)
{
	struct local_drm_i915_query_info info = {};

	if (igt_ioctl(fd, DRM_IOCTL_I915_QUERY_INFO, &info))
		return false;

	return info.version >= version;
}

static void test_query_version(int fd)
{
	struct local_drm_i915_query_info info = {};

	info.version = 0;
	do_ioctl(fd, DRM_IOCTL_I915_QUERY_INFO, &info);
	igt_assert_lte(1, info.version);

	info.version++;
	do_ioctl_err(fd, DRM_IOCTL_I915_QUERY_INFO, &info, EINVAL);
}

static void test_query_garbage(int fd)
{
	struct local_drm_i915_query_info info = {};

	info.version = 1;
	info.query = 0xffffffff;
	do_ioctl_err(fd, DRM_IOCTL_I915_QUERY_INFO, &info, EINVAL);

	info.version = 1;
	info.query = I915_QUERY_INFO_ENGINE;
	info.query_params[0] = LOCAL_I915_ENGINE_CLASS_RENDER;
	do_ioctl(fd, DRM_IOCTL_I915_QUERY_INFO, &info);
	igt_assert_neq(info.info_ptr_len, 0);

	info.info_ptr_len = 1;
	do_ioctl_err(fd, DRM_IOCTL_I915_QUERY_INFO, &info, EINVAL);
}

static void test_query_null_array(int fd)
{
	struct local_drm_i915_query_info info = {};

	info.version = 1;
	info.query = I915_QUERY_INFO_ENGINE;
	info.query_params[0] = LOCAL_I915_ENGINE_CLASS_RENDER;
	do_ioctl(fd, DRM_IOCTL_I915_QUERY_INFO, &info);

	info.info_ptr = 0;
	do_ioctl_err(fd, DRM_IOCTL_I915_QUERY_INFO, &info, EFAULT);
}

static unsigned int legacy_count_engines(int fd)
{
	unsigned int total = 0;
	const struct intel_execution_engine *e;

	for (e = intel_execution_engines; e->name; e++) {
		if (e->exec_id == 0)
			continue;

		if (!gem_has_ring(fd, e->exec_id | e->flags))
			continue;

		if (e->exec_id == I915_EXEC_BSD) {
			int is_bsd2 = e->flags != 0;
			if (gem_has_bsd2(fd) != is_bsd2)
				continue;
		}

		total++;
	}

	return total;
}

static void test_query_engine_classes(int fd)
{
	unsigned int legacy_num_engines = legacy_count_engines(fd);
	unsigned int num_engines = 0;
	enum local_drm_i915_engine_class class;

	for (class = 0; class < I915_ENGINE_CLASS_MAX; class++) {
		struct local_drm_i915_query_info info;
		struct local_drm_i915_engine_info *engines;
		unsigned i, j, num_class_engines;

		memset(&info, 0, sizeof(info));
		info.version = 1;
		info.query = I915_QUERY_INFO_ENGINE;
		info.query_params[0] = class;

		do_ioctl(fd, DRM_IOCTL_I915_QUERY_INFO, &info);

		igt_assert(info.info_ptr_len % sizeof(struct local_drm_i915_engine_info) == 0);

		num_class_engines = info.info_ptr_len / sizeof(struct local_drm_i915_engine_info);

		engines = calloc(num_class_engines, sizeof(struct local_drm_i915_engine_info));
		info.info_ptr = to_user_pointer(engines);
		do_ioctl(fd, DRM_IOCTL_I915_QUERY_INFO, &info);

		for (i = 0; i < info.info_ptr_len / sizeof(struct local_drm_i915_engine_info); i++) {
			for (j = 0; j < ARRAY_SIZE(engines[0].rsvd); j++)
				igt_assert_eq(engines[i].rsvd[j], 0);
		}
		free(engines);

		num_engines += num_class_engines;
	}

	igt_debug("num_engines=%u/%u\n", num_engines, legacy_num_engines);

	igt_assert_eq(num_engines, legacy_num_engines);
}

igt_main
{
	int fd = -1;

	igt_fixture {
		fd = drm_open_driver(DRIVER_INTEL);
		igt_require(query_info_supports(fd, 1 /* version */));
	}

	igt_subtest("query-version")
		test_query_version(fd);

	igt_subtest("query-garbage")
		test_query_garbage(fd);

	igt_subtest("query-null-array")
		test_query_null_array(fd);

	igt_subtest("query-engine-classes")
		test_query_engine_classes(fd);

	igt_fixture {
		close(fd);
	}
}
