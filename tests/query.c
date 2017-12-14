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

IGT_TEST_DESCRIPTION("Testing the query uAPI.");

static int
__i915_query(int fd, struct drm_i915_query *q)
{
	return igt_ioctl(fd, DRM_IOCTL_I915_QUERY, q);
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
		igt_assert_eq(__i915_query_item(fd, items, n_items), 0); \
		igt_assert_eq(errno, err); \
	} while (0)

static bool has_query_supports(int fd)
{
	struct drm_i915_query query = {};

	return __i915_query(fd, &query);
}

static void test_query_garbage(int fd)
{
	struct drm_i915_query_item items[2];
	struct drm_i915_query_item *items_ptr;
	int i, len;

	i915_query_item_err(fd, (void *) ULONG_MAX, 1, EFAULT);

	i915_query_item_err(fd, (void *) 0, 1, EFAULT);

	memset(items, 0, sizeof(items));
	items[0].query_id = ULONG_MAX;
	i915_query_item(fd, items, 2);
	igt_assert_eq(items[0].length, -EINVAL);

	memset(items, 0, sizeof(items));
	i915_query_item(fd, items, 2);
	igt_assert_eq(items[0].length, -EINVAL);
	igt_assert_eq(items[1].length, -EINVAL);

	memset(items, 0, sizeof(items));
	items[0].query_id = DRM_I915_QUERY_SLICE_INFO;
	items[1].query_id = DRM_I915_QUERY_SUBSLICE_INFO;
	items[1].length = sizeof(struct drm_i915_query_subslice_info) - 1;
	i915_query_item(fd, items, 2);
	igt_assert_eq(items[0].length, 0);
	igt_assert_eq(items[1].length, -EINVAL);

	items_ptr = mmap(0, 4096, PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	items_ptr[0].query_id = DRM_I915_QUERY_SLICE_INFO;
	i915_query_item(fd, items_ptr, 1);
	igt_assert(items_ptr[0].length >= sizeof(struct drm_i915_query_slice_info));
	munmap(items_ptr, 4096);
	i915_query_item_err(fd, items_ptr, 1, EFAULT);

	len = sizeof(struct drm_i915_query_item) * 10;
	items_ptr = mmap(0, len, PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	for (i = 0; i < 10; i++)
		items_ptr[i].query_id = DRM_I915_QUERY_SLICE_INFO;
	igt_assert_eq(__i915_query_item(fd, items_ptr,
					INT_MAX / sizeof(struct drm_i915_query_item) + 1), -1);
	igt_assert(errno == EFAULT || errno == EINVAL);
	munmap(items_ptr, len);
}

static bool query_topology_supported(int fd)
{
	struct drm_i915_query_item item = {
		.query_id = DRM_I915_QUERY_SLICE_INFO,
	};

	return __i915_query_item(fd, &item, 1) == 0;
}

static void test_query_topology_pre_gen8(int fd)
{
	struct drm_i915_query_item item = {
		.query_id = DRM_I915_QUERY_SLICE_INFO,
	};

	i915_query_item(fd, &item, 1);
	igt_assert_eq(item.length, -ENODEV);
}

static void
test_query_topology_coherent_slice_mask(int fd)
{
	struct drm_i915_query_item item;
	struct drm_i915_query_slice_info *slices_info;
	struct drm_i915_query_subslice_info *subslices_info;
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
	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_SLICE_INFO;
	i915_query_item(fd, &item, 1);
	igt_assert(item.length > 0);

	slices_info = (struct drm_i915_query_slice_info *) calloc(1, item.length);

	item.data_ptr = to_user_pointer(slices_info);
	i915_query_item(fd, &item, 1);
	igt_assert(item.length > 0);

	topology_slices = 0;
	for (i = 0; i < slices_info->max_slices; i++) {
		if (DRM_I915_QUERY_SLICE_AVAILABLE(slices_info, i))
			topology_slices++;
	}

	/* These 2 should always match. */
	igt_assert(__builtin_popcount(slice_mask) == topology_slices);

	free(slices_info);

	/* Subslices */
	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_SUBSLICE_INFO;
	i915_query_item(fd, &item, 1);
	igt_assert(item.length > 0);

	subslices_info = (struct drm_i915_query_subslice_info *) calloc(1, item.length);

	item.data_ptr = (uintptr_t) subslices_info;
	i915_query_item(fd, &item, 1);
	igt_assert(item.length > 0);

	topology_subslices_slice0 = 0;
	for (i = 0; i < subslices_info->max_subslices; i++) {
		topology_subslices_slice0 +=
			__builtin_popcount(
				DRM_I915_QUERY_SUBSLICE_AVAILABLE(subslices_info, 0, i));
	}

	/* I915_PARAM_SUBSLICE_INFO returns the value for slice0, we
	 * should match the values for the first slice of the
	 * topology.
	 */
	igt_assert(__builtin_popcount(subslice_mask) == topology_subslices_slice0);

	free(subslices_info);
}

static void
test_query_topology_matches_eu_total(int fd)
{
	struct drm_i915_query_item item;
	struct drm_i915_query_eu_info *eus_info;
	drm_i915_getparam_t gp;
	int n_eus, n_eus_topology, s;

	gp.param = I915_PARAM_EU_TOTAL;
	gp.value = &n_eus;
	do_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp);
	igt_debug("n_eus=%i\n", n_eus);

	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_EU_INFO;
	i915_query_item(fd, &item, 1);

	eus_info = (struct drm_i915_query_eu_info *) calloc(1, item.length);

	item.data_ptr = to_user_pointer(eus_info);
	i915_query_item(fd, &item, 1);

	igt_debug("max_slices=%u max_subslices=%u max_eus_per_subslice=%u\n",
		  eus_info->max_slices, eus_info->max_subslices,
		  eus_info->max_eus_per_subslice);

	n_eus_topology = 0;
	for (s = 0; s < eus_info->max_slices; s++) {
		int ss;

		igt_debug("slice%i:\n", s);

		for (ss = 0; ss < eus_info->max_subslices; ss++) {
			int eu, n_subslice_eus = 0;

			igt_debug("\tsubslice: %i\n", ss);

			igt_debug("\t\teu_mask: 0b");
			for (eu = 0; eu < eus_info->max_eus_per_subslice; eu++) {
				uint8_t val = DRM_I915_QUERY_EU_AVAILABLE(eus_info, s, ss,
									  eus_info->max_eus_per_subslice - 1 - eu);
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
