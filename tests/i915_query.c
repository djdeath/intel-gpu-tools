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

#include <limits.h>

IGT_TEST_DESCRIPTION("Testing the i915 query uAPI.");

static int
__i915_query(int fd, struct drm_i915_query *q)
{
	if (igt_ioctl(fd, DRM_IOCTL_I915_QUERY, q))
		return -errno;
	return 0;
}

static int
__i915_query_item(int fd, struct drm_i915_query_item *items, uint32_t n_items)
{
	struct drm_i915_query q = {
		.num_items = n_items,
		.items_ptr = to_user_pointer(items),
	};
	return __i915_query(fd, &q);
}

#define i915_query_item(fd, items, n_items) do { \
		igt_assert_eq(__i915_query_item(fd, items, n_items), 0); \
		errno = 0; \
	} while (0)
#define i915_query_item_err(fd, items, n_items, err) do { \
		igt_assert_eq(__i915_query_item(fd, items, n_items), -err); \
	} while (0)

static bool has_query_supports(int fd)
{
	struct drm_i915_query query = {};

	return __i915_query(fd, &query) == 0;
}

static void test_query_garbage(int fd)
{
	struct drm_i915_query_item items[2];
	struct drm_i915_query_item *items_ptr;
	int i, len, ret;

	i915_query_item_err(fd, (void *) ULONG_MAX, 1, EFAULT);

	i915_query_item_err(fd, (void *) 0, 1, EFAULT);

	memset(items, 0, sizeof(items));
	i915_query_item_err(fd, items, 1, EINVAL);

	memset(items, 0, sizeof(items));
	items[0].query_id = ULONG_MAX;
	items[1].query_id = ULONG_MAX - 2;
	i915_query_item(fd, items, 2);
	igt_assert_eq(items[0].length, -EINVAL);
	igt_assert_eq(items[1].length, -EINVAL);

	memset(items, 0, sizeof(items));
	items[0].query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	items[1].query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	items[1].length = sizeof(struct drm_i915_query_topology_info) - 1;
	i915_query_item(fd, items, 2);
	igt_assert_lte(0, items[0].length);
	igt_assert_eq(items[1].length, -EINVAL);

	items_ptr = mmap(0, 4096, PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	items_ptr[0].query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	i915_query_item(fd, items_ptr, 1);
	igt_assert(items_ptr[0].length >= sizeof(struct drm_i915_query_topology_info));
	munmap(items_ptr, 4096);
	i915_query_item_err(fd, items_ptr, 1, EFAULT);

	len = sizeof(struct drm_i915_query_item) * 10;
	items_ptr = mmap(0, len, PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	for (i = 0; i < 10; i++)
		items_ptr[i].query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	ret = __i915_query_item(fd, items_ptr,
				INT_MAX / sizeof(struct drm_i915_query_item) + 1);
	igt_assert(ret == -EFAULT || ret == -EINVAL);
	munmap(items_ptr, len);
}

static bool query_topology_supported(int fd)
{
	struct drm_i915_query_item item = {
		.query_id = DRM_I915_QUERY_TOPOLOGY_INFO,
	};

	return __i915_query_item(fd, &item, 1) == 0 && item.length > 0;
}

static void test_query_topology_pre_gen8(int fd)
{
	struct drm_i915_query_item item = {
		.query_id = DRM_I915_QUERY_TOPOLOGY_INFO,
	};

	i915_query_item(fd, &item, 1);
	igt_assert_eq(item.length, -ENODEV);
}

#define DIV_ROUND_UP(val, div) (ALIGN(val, div) / div)

static bool
slice_available(const struct drm_i915_query_topology_info *topo_info,
		int s)
{
	return (topo_info->data[s / 8] >> (s % 8)) & 1;
}

static bool
subslice_available(const struct drm_i915_query_topology_info *topo_info,
		   int s, int ss)
{
	return (topo_info->data[topo_info->subslice_offset +
				s * topo_info->subslice_stride +
				ss / 8] >> (ss % 8)) & 1;
}

static bool
eu_available(const struct drm_i915_query_topology_info *topo_info,
	     int s, int ss, int eu)
{
	return (topo_info->data[topo_info->eu_offset +
				(s * topo_info->max_subslices + ss) * topo_info->eu_stride +
				eu / 8] >> (eu % 8)) & 1;
}

static void
test_query_topology_coherent_slice_mask(int fd)
{
	struct drm_i915_query_item item;
	uint8_t *_topo_info;
	struct drm_i915_query_topology_info *topo_info;
	drm_i915_getparam_t gp;
	int slice_mask, subslice_mask;
	int s, topology_slices, topology_subslices_slice0;

	gp.param = I915_PARAM_SLICE_MASK;
	gp.value = &slice_mask;
	igt_skip_on(igt_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp) != 0);

	gp.param = I915_PARAM_SUBSLICE_MASK;
	gp.value = &subslice_mask;
	igt_skip_on(igt_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp) != 0);

	/* Slices */
	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	i915_query_item(fd, &item, 1);
	igt_assert(item.length > 0);

	_topo_info = malloc(3 * item.length);
	memset(_topo_info, 0xff, 3 * item.length);
	topo_info = (struct drm_i915_query_topology_info *) (_topo_info + item.length);
	memset(topo_info, 0, item.length);
	igt_assert(topo_info);

	item.data_ptr = to_user_pointer(topo_info);
	i915_query_item(fd, &item, 1);
	igt_assert(item.length > 0);

	topology_slices = 0;
	for (s = 0; s < topo_info->max_slices; s++) {
		if (slice_available(topo_info, s))
			topology_slices |= 1UL << s;
	}

	igt_debug("slice mask getparam=0x%x / query=0x%x\n",
		  slice_mask, topology_slices);

	/* These 2 should always match. */
	igt_assert_eq_u32(slice_mask, topology_slices);

	topology_subslices_slice0 = 0;
	for (s = 0; s < topo_info->max_subslices; s++) {
		if (subslice_available(topo_info, 0, s))
			topology_subslices_slice0 |= 1UL << s;
	}

	igt_debug("subslice mask getparam=0x%x / query=0x%x\n",
		  subslice_mask, topology_subslices_slice0);

	/* I915_PARAM_SUBSLICE_MASK returns the value for slice0, we
	 * should match the values for the first slice of the
	 * topology.
	 */
	igt_assert_eq_u32(subslice_mask, topology_subslices_slice0);

	for (s = 0; s < item.length; s++) {
		igt_assert_eq(_topo_info[s], 0xff);
		igt_assert_eq(_topo_info[2 * item.length + s], 0xff);
	}

	free(_topo_info);
}

static void
test_query_topology_matches_eu_total(int fd)
{
	struct drm_i915_query_item item;
	struct drm_i915_query_topology_info *topo_info;
	drm_i915_getparam_t gp;
	int n_eus, n_eus_topology, s;

	gp.param = I915_PARAM_EU_TOTAL;
	gp.value = &n_eus;
	do_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);
	igt_debug("n_eus=%i\n", n_eus);

	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	i915_query_item(fd, &item, 1);

	topo_info = (struct drm_i915_query_topology_info *) calloc(1, item.length);

	item.data_ptr = to_user_pointer(topo_info);
	i915_query_item(fd, &item, 1);

	igt_debug("max_slices=%hu max_subslices=%hu max_eus_per_subslice=%hu\n",
		  topo_info->max_slices, topo_info->max_subslices,
		  topo_info->max_eus_per_subslice);
	igt_debug(" subslice_offset=%hu subslice_stride=%hu\n",
		  topo_info->subslice_offset, topo_info->subslice_stride);
	igt_debug(" eu_offset=%hu eu_stride=%hu\n",
		  topo_info->eu_offset, topo_info->eu_stride);

	n_eus_topology = 0;
	for (s = 0; s < topo_info->max_slices; s++) {
		int ss;

		igt_debug("slice%i:\n", s);

		for (ss = 0; ss < topo_info->max_subslices; ss++) {
			int eu, n_subslice_eus = 0;

			igt_debug("\tsubslice: %i\n", ss);

			igt_debug("\t\teu_mask: 0b");
			for (eu = 0; eu < topo_info->max_eus_per_subslice; eu++) {
				uint8_t val = eu_available(topo_info, s, ss,
							   topo_info->max_eus_per_subslice - 1 - eu);
				igt_debug("%hhi", val);
				n_subslice_eus += __builtin_popcount(val);
				n_eus_topology += __builtin_popcount(val);
			}
			igt_debug(" (%i)\n", n_subslice_eus);
		}
	}

	igt_assert(n_eus_topology == n_eus);
}

igt_main
{
	int fd = -1;
	int devid;

	igt_fixture {
		fd = drm_open_driver(DRIVER_INTEL);
		igt_require(has_query_supports(fd));
		devid = intel_get_drm_devid(fd);
	}

	igt_subtest("query-garbage")
		test_query_garbage(fd);

	igt_subtest("query-topology-pre-gen8") {
		igt_require(intel_gen(devid) < 8 && !IS_HASWELL(devid));
		igt_require(query_topology_supported(fd));
		test_query_topology_pre_gen8(fd);
	}

	igt_subtest("query-topology-coherent-slice-mask") {
		igt_require(AT_LEAST_GEN(devid, 8) || IS_HASWELL(devid));
		igt_require(query_topology_supported(fd));
		test_query_topology_coherent_slice_mask(fd);
	}

	igt_subtest("query-topology-matches-eu-total") {
		igt_require(AT_LEAST_GEN(devid, 8) || IS_HASWELL(devid));
		igt_require(query_topology_supported(fd));
		test_query_topology_matches_eu_total(fd);
	}

	igt_fixture {
		close(fd);
	}
}
