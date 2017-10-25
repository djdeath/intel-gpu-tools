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

#ifndef I915_PARAM_SLICE_MASK
#define I915_PARAM_SLICE_MASK		 46
#endif

#ifndef I915_PARAM_SUBSLICE_MASK
#define I915_PARAM_SUBSLICE_MASK	 47
#endif

#ifndef I915_QUERY_INFO_RCS_TOPOLOGY
/* Query RCS topology.
 *
 * drm_i915_query_info.query_params[0] should be set to one of the
 * I915_RCS_TOPOLOGY_* define.
 *
 * drm_i915_gem_query_info.info_ptr will be written to with
 * drm_i915_rcs_topology_info.
 */
#define I915_QUERY_INFO_RCS_TOPOLOGY	1 /* version 1 */

/* Query RCS slice topology
 *
 * The meaning of the drm_i915_rcs_topology_info fields is :
 *
 * params[0]: number of slices
 *
 * data: Each bit indicates whether a slice is available (1) or fused off (0).
 * Formula to tell if slice X is available :
 *
 *         (data[X / 8] >> (X % 8)) & 1
 */
#define I915_RCS_TOPOLOGY_SLICE		0 /* version 1 */
/* Query RCS subslice topology
 *
 * The meaning of the drm_i915_rcs_topology_info fields is :
 *
 * params[0]: number of slices
 * params[1]: slice stride
 *
 * data: each bit indicates whether a subslice is available (1) or fused off
 * (0). Formula to tell if slice X subslice Y is available :
 *
 *         (data[(X * params[1]) + Y / 8] >> (Y % 8)) & 1
 */
#define I915_RCS_TOPOLOGY_SUBSLICE	1 /* version 1 */
/* Query RCS EU topology
 *
 * The meaning of the drm_i915_rcs_topology_info fields is :
 *
 * params[0]: number of slices
 * params[1]: slice stride
 * params[2]: subslice stride
 *
 * data: Each bit indicates whether a subslice is available (1) or fused off
 * (0). Formula to tell if slice X subslice Y eu Z is available :
 *
 *         (data[X * params[1] + Y * params[2] + Z / 8] >> (Z % 8)) & 1
 */
#define I915_RCS_TOPOLOGY_EU		2 /* version 1 */
#endif /* I915_QUERY_INFO_RCS_TOPOLOGY */

struct local_drm_i915_rcs_topology_info {
	__u32 params[6];

	__u8 data[];
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

#define LOCAL_I915_EXEC_CLASS_INSTANCE	(1<<19)

#define LOCAL_I915_EXEC_INSTANCE_SHIFT	(20)
#define LOCAL_I915_EXEC_INSTANCE_MASK	(0xff << LOCAL_I915_EXEC_INSTANCE_SHIFT)

#define local_i915_execbuffer2_engine(class, instance) \
	(LOCAL_I915_EXEC_CLASS_INSTANCE | \
	(class) | \
	((instance) << LOCAL_I915_EXEC_INSTANCE_SHIFT))

static int exec_noop(int fd, bool mustpass,
		     enum local_drm_i915_engine_class class,
		     uint8_t instance)
{
	uint32_t bbe = MI_BATCH_BUFFER_END;
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 exec;
	int ret;

	memset(&exec, 0, sizeof(exec));
	exec.handle = gem_create(fd, 4096);
	gem_write(fd, exec.handle, 0, &bbe, sizeof(bbe));

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = to_user_pointer(&exec);
	execbuf.buffer_count = 1;
	execbuf.flags = local_i915_execbuffer2_engine(class, instance);
	ret = __gem_execbuf(fd, &execbuf);
	if (mustpass)
		igt_assert_eq(ret, 0);
	gem_close(fd, exec.handle);

	return ret;
}

static void test_query_engine_exec_class_instance(int fd)
{
	enum local_drm_i915_engine_class class;

	for (class = 0; class < I915_ENGINE_CLASS_MAX; class++) {
		struct local_drm_i915_engine_info *engines;
		struct local_drm_i915_query_info info = {};
		unsigned engine, num_engines;

		info.version = 1;
		info.query = I915_QUERY_INFO_ENGINE;
		info.query_params[0] = class;

		do_ioctl(fd, DRM_IOCTL_I915_QUERY_INFO, &info);

		num_engines = info.info_ptr_len / sizeof(*engines);
		engines = calloc(num_engines, sizeof(*engines));

		for (engine = 0; engine < num_engines; engine++)
			exec_noop(fd, true, class, engines[engine].instance);
		exec_noop(fd, false, class, ~0);

		free(engines);
	}

}

static bool query_topology_supported(int fd)
{
	struct local_drm_i915_query_info info = {};

	info.version = 1;
	info.query = I915_QUERY_INFO_RCS_TOPOLOGY;
	info.query_params[0] = I915_RCS_TOPOLOGY_SLICE;

	return igt_ioctl(fd, DRM_IOCTL_I915_QUERY_INFO, &info) == 0;
}

static void test_query_topology_pre_gen8(int fd)
{
	struct local_drm_i915_query_info info = {};

	info.version = 1;
	info.query = I915_QUERY_INFO_RCS_TOPOLOGY;
	info.query_params[0] = I915_RCS_TOPOLOGY_SLICE;

	do_ioctl_err(fd, DRM_IOCTL_I915_QUERY_INFO, &info, ENODEV);
}

static void
test_query_topology_coherent_slice_mask(int fd)
{
	struct local_drm_i915_query_info info;
	struct local_drm_i915_rcs_topology_info *topology;
	drm_i915_getparam_t gp;
	int slice_mask, subslice_mask;
	int i, topology_slices, topology_subslices_slice0;

	gp.param = I915_PARAM_SLICE_MASK;
	gp.value = &slice_mask;
	do_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);

	gp.param = I915_PARAM_SUBSLICE_MASK;
	gp.value = &subslice_mask;
	do_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);

	igt_debug("slice_mask=0x%x subslice_mask=0x%x\n", slice_mask, subslice_mask);

	/* Slices */
	memset(&info, 0, sizeof(info));
	info.version = 1;
	info.query = I915_QUERY_INFO_RCS_TOPOLOGY;
	info.query_params[0] = I915_RCS_TOPOLOGY_SLICE;
	do_ioctl(fd, DRM_IOCTL_I915_QUERY_INFO, &info);
	igt_assert_neq(info.info_ptr_len, 0);

	topology = calloc(1, info.info_ptr_len);
	info.info_ptr = to_user_pointer(topology);
	do_ioctl(fd, DRM_IOCTL_I915_QUERY_INFO, &info);

	topology_slices = 0;
	for (i = 0; i < (topology->params[0] / 8) + 1; i++)
		topology_slices += __builtin_popcount(topology->data[i]);

	/* These 2 should always match. */
	igt_assert(__builtin_popcount(slice_mask) == topology_slices);

	free(topology);

	/* Subslices */
	memset(&info, 0, sizeof(info));
	info.version = 1;
	info.query = I915_QUERY_INFO_RCS_TOPOLOGY;
	info.query_params[0] = I915_RCS_TOPOLOGY_SUBSLICE;
	do_ioctl(fd, DRM_IOCTL_I915_QUERY_INFO, &info);
	igt_assert_neq(info.info_ptr_len, 0);

	topology = calloc(1, info.info_ptr_len);
	info.info_ptr = to_user_pointer(topology);
	do_ioctl(fd, DRM_IOCTL_I915_QUERY_INFO, &info);

	topology_subslices_slice0 = 0;
	for (i = 0; i < topology->params[1]; i++)
		topology_subslices_slice0 += __builtin_popcount(topology->data[i]);

	/* I915_PARAM_SUBSLICE_MASK returns the value for slice0, we
	 * should match the values for the first slice of the
	 * topology.
	 */
	igt_assert(__builtin_popcount(subslice_mask) == topology_subslices_slice0);

	free(topology);
}

static void
test_query_topology_matches_eu_total(int fd)
{
	struct local_drm_i915_query_info info;
	struct local_drm_i915_rcs_topology_info *topology;
	drm_i915_getparam_t gp;
	int n_eus, n_eus_topology, s;

	gp.param = I915_PARAM_EU_TOTAL;
	gp.value = &n_eus;
	do_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);
	igt_debug("legacy n_eus=%i\n", n_eus);

	memset(&info, 0, sizeof(info));
	info.version = 1;
	info.query = I915_QUERY_INFO_RCS_TOPOLOGY;
	info.query_params[0] = I915_RCS_TOPOLOGY_EU;
	do_ioctl(fd, DRM_IOCTL_I915_QUERY_INFO, &info);

	topology = calloc(1, info.info_ptr_len);
	info.info_ptr = to_user_pointer(topology);
	do_ioctl(fd, DRM_IOCTL_I915_QUERY_INFO, &info);

	n_eus_topology = 0;
	for (s = 0; s < topology->params[0]; s++) {
		int ss;

		igt_debug("slice%i:\n", s);

		for (ss = 0; ss < topology->params[1] / topology->params[2]; ss++) {
			int eu, n_subslice_eus = 0;

			igt_debug("\tsubslice: %i\n", ss);

			igt_debug("\t\teu_mask:");
			for (eu = 0; eu < topology->params[2]; eu++) {
				uint8_t val = topology->data[s * topology->params[1] +
							     ss * topology->params[2] + eu];
				igt_debug(" 0x%hhx", val);
				n_subslice_eus += __builtin_popcount(val);
				n_eus_topology += __builtin_popcount(val);
			}
			igt_debug(" (%i)\n", n_subslice_eus);
		}
	}
	igt_debug("topology n_eus=%i\n", n_eus_topology);

	igt_assert(n_eus_topology == n_eus);
	free(topology);
}

igt_main
{
	int fd = -1;
	int devid;

	igt_fixture {
		fd = drm_open_driver(DRIVER_INTEL);
		igt_require(query_info_supports(fd, 1 /* version */));
		devid = intel_get_drm_devid(fd);
	}

	igt_subtest("query-version")
		test_query_version(fd);

	igt_subtest("query-garbage")
		test_query_garbage(fd);

	igt_subtest("query-null-array")
		test_query_null_array(fd);

	igt_subtest("query-engine-classes")
		test_query_engine_classes(fd);

	igt_subtest("query-engine-exec-class-instance")
		test_query_engine_exec_class_instance(fd);

	igt_subtest("query-topology-pre-gen8") {
		igt_require(intel_gen(devid) < 8);
		igt_require(query_topology_supported(fd));
		test_query_topology_pre_gen8(fd);
	}

	igt_subtest("query-topology-coherent-slice-mask") {
		igt_require(AT_LEAST_GEN(devid, 8));
		igt_require(query_topology_supported(fd));
		test_query_topology_coherent_slice_mask(fd);
	}

	igt_subtest("query-topology-matches-eu-total") {
		igt_require(AT_LEAST_GEN(devid, 8));
		igt_require(query_topology_supported(fd));
		test_query_topology_matches_eu_total(fd);
	}

	igt_fixture {
		close(fd);
	}
}
