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
#include "igt_sysfs.h"

#include <limits.h>

IGT_TEST_DESCRIPTION("Testing the i915 query uAPI.");

/*
 * We should at least get 3 bytes for data for each slices, subslices & EUs
 * masks.
 */
#define MIN_TOPOLOGY_ITEM_SIZE (sizeof(struct drm_i915_query_topology_info) + 3)

static int
__i915_query(int fd, struct drm_i915_query *q)
{
	if (igt_ioctl(fd, DRM_IOCTL_I915_QUERY, q))
		return -errno;
	return 0;
}

static int
__i915_query_items(int fd, struct drm_i915_query_item *items, uint32_t n_items)
{
	struct drm_i915_query q = {
		.num_items = n_items,
		.items_ptr = to_user_pointer(items),
	};
	return __i915_query(fd, &q);
}

#define i915_query_items(fd, items, n_items) do { \
		igt_assert_eq(__i915_query_items(fd, items, n_items), 0); \
		errno = 0; \
	} while (0)
#define i915_query_items_err(fd, items, n_items, err) do { \
		igt_assert_eq(__i915_query_items(fd, items, n_items), -err); \
	} while (0)

static bool has_query_supports(int fd)
{
	struct drm_i915_query query = {};

	return __i915_query(fd, &query) == 0;
}

static void test_query_garbage(int fd)
{
	struct drm_i915_query query;
	struct drm_i915_query_item item;

	/* Verify that invalid query pointers are rejected. */
	igt_assert_eq(__i915_query(fd, NULL), -EFAULT);
	igt_assert_eq(__i915_query(fd, (void *) -1), -EFAULT);

	/*
	 * Query flags field is currently valid only if equals to 0. This might
	 * change in the future.
	 */
	memset(&query, 0, sizeof(query));
	query.flags = 42;
	igt_assert_eq(__i915_query(fd, &query), -EINVAL);

	/* Test a couple of invalid pointers. */
	i915_query_items_err(fd, (void *) ULONG_MAX, 1, EFAULT);
	i915_query_items_err(fd, (void *) 0, 1, EFAULT);

	/* Test the invalid query id = 0. */
	memset(&item, 0, sizeof(item));
	i915_query_items_err(fd, &item, 1, EINVAL);
}

static void test_query_garbage_items(int fd)
{
	struct drm_i915_query_item items[2];
	struct drm_i915_query_item *items_ptr;
	int i, n_items;

	/*
	 * Query item flags field is currently valid only if equals to 0.
	 * Subject to change in the future.
	 */
	memset(items, 0, sizeof(items));
	items[0].query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	items[0].flags = 42;
	i915_query_items(fd, items, 1);
	igt_assert_eq(items[0].length, -EINVAL);

	/*
	 * Test an invalid query id in the second item and verify that the first
	 * one is properly processed.
	 */
	memset(items, 0, sizeof(items));
	items[0].query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	items[1].query_id = ULONG_MAX;
	i915_query_items(fd, items, 2);
	igt_assert_lte(MIN_TOPOLOGY_ITEM_SIZE, items[0].length);
	igt_assert_eq(items[1].length, -EINVAL);

	/*
	 * Test a invalid query id in the first item and verify that the second
	 * one is properly processed (the driver is expected to go through them
	 * all and place error codes in the failed items).
	 */
	memset(items, 0, sizeof(items));
	items[0].query_id = ULONG_MAX;
	items[1].query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	i915_query_items(fd, items, 2);
	igt_assert_eq(items[0].length, -EINVAL);
	igt_assert_lte(MIN_TOPOLOGY_ITEM_SIZE, items[1].length);

	/* Test a couple of invalid data pointer in query item. */
	memset(items, 0, sizeof(items));
	items[0].query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	i915_query_items(fd, items, 1);
	igt_assert_lte(MIN_TOPOLOGY_ITEM_SIZE, items[0].length);

	items[0].data_ptr = 0;
	i915_query_items(fd, items, 1);
	igt_assert_eq(items[0].length, -EFAULT);

	items[0].data_ptr = ULONG_MAX;
	i915_query_items(fd, items, 1);
	igt_assert_eq(items[0].length, -EFAULT);


	/* Test an invalid query item length. */
	memset(items, 0, sizeof(items));
	items[0].query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	items[1].query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	items[1].length = sizeof(struct drm_i915_query_topology_info) - 1;
	i915_query_items(fd, items, 2);
	igt_assert_lte(MIN_TOPOLOGY_ITEM_SIZE, items[0].length);
	igt_assert_eq(items[1].length, -EINVAL);

	/*
	 * Map memory for a query item in which the kernel is going to write the
	 * length of the item in the first ioctl(). Then unmap that memory and
	 * verify that the kernel correctly returns EFAULT as memory of the item
	 * has been removed from our address space.
	 */
	items_ptr = mmap(0, 4096, PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	items_ptr[0].query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	i915_query_items(fd, items_ptr, 1);
	igt_assert_lte(MIN_TOPOLOGY_ITEM_SIZE, items_ptr[0].length);
	munmap(items_ptr, 4096);
	i915_query_items_err(fd, items_ptr, 1, EFAULT);

	/*
	 * Map memory for a query item, then make it read only and verify that
	 * the kernel errors out with EFAULT.
	 */
	items_ptr = mmap(0, 4096, PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	items_ptr[0].query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	igt_assert_eq(0, mprotect(items_ptr, 4096, PROT_READ));
	i915_query_items_err(fd, items_ptr, 1, EFAULT);
	munmap(items_ptr, 4096);

	/*
	 * Allocate 2 pages, prepare those 2 pages with valid query items, then
	 * switch the second page to read only and expect an EFAULT error.
	 */
	items_ptr = mmap(0, 8192, PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	memset(items_ptr, 0, 8192);
	n_items = 8192 / sizeof(struct drm_i915_query_item);
	for (i = 0; i < n_items; i++)
		items_ptr[i].query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	mprotect(((uint8_t *)items_ptr) + 4096, 4096, PROT_READ);
	i915_query_items_err(fd, items_ptr, n_items, EFAULT);
	munmap(items_ptr, 8192);
}

/*
 * Allocate more on both sides of where the kernel is going to write and verify
 * that it writes only where it's supposed to.
 */
static void test_query_topology_kernel_writes(int fd)
{
	struct drm_i915_query_item item;
	struct drm_i915_query_topology_info *topo_info;
	uint8_t *_topo_info;
	int b, total_size;

	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	i915_query_items(fd, &item, 1);
	igt_assert_lte(MIN_TOPOLOGY_ITEM_SIZE, item.length);

	total_size = item.length + 2 * sizeof(*_topo_info);
	_topo_info = malloc(total_size);
	memset(_topo_info, 0xff, total_size);
	topo_info = (struct drm_i915_query_topology_info *) (_topo_info + sizeof(*_topo_info));
	memset(topo_info, 0, item.length);

	item.data_ptr = to_user_pointer(topo_info);
	i915_query_items(fd, &item, 1);

	for (b = 0; b < sizeof(*_topo_info); b++) {
		igt_assert_eq(_topo_info[b], 0xff);
		igt_assert_eq(_topo_info[sizeof(*_topo_info) + item.length + b], 0xff);
	}
}

static bool query_topology_supported(int fd)
{
	struct drm_i915_query_item item = {
		.query_id = DRM_I915_QUERY_TOPOLOGY_INFO,
	};

	return __i915_query_items(fd, &item, 1) == 0 && item.length > 0;
}

static void test_query_topology_unsupported(int fd)
{
	struct drm_i915_query_item item = {
		.query_id = DRM_I915_QUERY_TOPOLOGY_INFO,
	};

	i915_query_items(fd, &item, 1);
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

/*
 * Verify that we get coherent values between the legacy getparam slice/subslice
 * masks and the new topology query.
 */
static void
test_query_topology_coherent_slice_mask(int fd)
{
	struct drm_i915_query_item item;
	struct drm_i915_query_topology_info *topo_info;
	drm_i915_getparam_t gp;
	int slice_mask, subslice_mask;
	int s, topology_slices, topology_subslices_slice0;
	int32_t first_query_length;

	gp.param = I915_PARAM_SLICE_MASK;
	gp.value = &slice_mask;
	igt_skip_on(igt_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp) != 0);

	gp.param = I915_PARAM_SUBSLICE_MASK;
	gp.value = &subslice_mask;
	igt_skip_on(igt_ioctl(fd, DRM_IOCTL_I915_GETPARAM, &gp) != 0);

	/* Slices */
	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	i915_query_items(fd, &item, 1);
	/* We expect at least one byte for each slices, subslices & EUs masks. */
	igt_assert_lte(MIN_TOPOLOGY_ITEM_SIZE, item.length);
	first_query_length = item.length;

	topo_info = calloc(1, item.length);

	item.data_ptr = to_user_pointer(topo_info);
	i915_query_items(fd, &item, 1);
	/* We should get the same size once the data has been written. */
	igt_assert_eq(first_query_length, item.length);
	/* We expect at least one byte for each slices, subslices & EUs masks. */
	igt_assert_lte(MIN_TOPOLOGY_ITEM_SIZE, item.length);

	topology_slices = 0;
	for (s = 0; s < topo_info->max_slices; s++) {
		if (slice_available(topo_info, s))
			topology_slices |= 1UL << s;
	}

	igt_debug("slice mask getparam=0x%x / query=0x%x\n",
		  slice_mask, topology_slices);

	/* These 2 should always match. */
	igt_assert_eq(slice_mask, topology_slices);

	topology_subslices_slice0 = 0;
	for (s = 0; s < topo_info->max_subslices; s++) {
		if (subslice_available(topo_info, 0, s))
			topology_subslices_slice0 |= 1UL << s;
	}

	igt_debug("subslice mask getparam=0x%x / query=0x%x\n",
		  subslice_mask, topology_subslices_slice0);

	/*
	 * I915_PARAM_SUBSLICE_MASK returns the value for slice0, we should
	 * match the values for the first slice of the topology.
	 */
	igt_assert_eq(subslice_mask, topology_subslices_slice0);

	free(topo_info);
}

/*
 * Verify that we get same total number of EUs from getparam and topology query.
 */
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
	i915_query_items(fd, &item, 1);

	topo_info = calloc(1, item.length);

	item.data_ptr = to_user_pointer(topo_info);
	i915_query_items(fd, &item, 1);

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

			/* Sanity checks. */
			if (n_subslice_eus > 0) {
				igt_assert(slice_available(topo_info, s));
				igt_assert(subslice_available(topo_info, s, ss));
			}
			if (subslice_available(topo_info, s, ss)) {
				igt_assert(slice_available(topo_info, s));
			}
		}
	}

	free(topo_info);

	igt_assert(n_eus_topology == n_eus);
}

/*
 * Verify some numbers on Gens that we know for sure the characteristics from
 * the PCI ids.
 */
static void
test_query_topology_known_pci_ids(int fd, int devid)
{
	const struct intel_device_info *dev_info = intel_get_device_info(devid);
	struct drm_i915_query_item item;
	struct drm_i915_query_topology_info *topo_info;
	int n_slices = 0, n_subslices = 0;
	int s, ss;

	/* The GT size on some Broadwell skus is not defined, skip those. */
	igt_skip_on(dev_info->gt == 0);

	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_TOPOLOGY_INFO;
	i915_query_items(fd, &item, 1);

	topo_info = (struct drm_i915_query_topology_info *) calloc(1, item.length);

	item.data_ptr = to_user_pointer(topo_info);
	i915_query_items(fd, &item, 1);

	for (s = 0; s < topo_info->max_slices; s++) {
		if (slice_available(topo_info, s))
			n_slices++;

		for (ss = 0; ss < topo_info->max_subslices; ss++) {
			if (subslice_available(topo_info, s, ss))
				n_subslices++;
		}
	}

	igt_debug("Platform=%s GT=%u slices=%u subslices=%u\n",
		  dev_info->codename, dev_info->gt, n_slices, n_subslices);

	switch (dev_info->gt) {
	case 1:
		igt_assert_eq(n_slices, 1);
		igt_assert(n_subslices == 2 || n_subslices == 3);
		break;
	case 2:
		igt_assert_eq(n_slices, 1);
		if (dev_info->is_haswell)
			igt_assert_eq(n_subslices, 2);
		else
			igt_assert_eq(n_subslices, 3);
		break;
	case 3:
		igt_assert_eq(n_slices, 2);
		if (dev_info->is_haswell)
			igt_assert_eq(n_subslices, 2 * 2);
		else
			igt_assert_eq(n_subslices, 2 * 3);
		break;
	case 4:
		igt_assert_eq(n_slices, 3);
		igt_assert_eq(n_subslices, 3 * 3);
		break;
	default:
		igt_assert(false);
	}

	free(topo_info);
}

static bool query_perf_config_supported(int fd)
{
	struct drm_i915_query_item item = {
		.query_id = DRM_I915_QUERY_PERF_CONFIG,
		.flags = DRM_I915_QUERY_PERF_CONFIG_LIST,
	};

	return __i915_query_items(fd, &item, 1) == 0 && item.length > 0;
}

/*
 * Verify that perf configuration queries for list of configurations
 * rejects invalid parameters.
 */
static void test_query_perf_config_list_invalid(int fd)
{
	struct drm_i915_query_perf_config *query_config_ptr;
	struct drm_i915_query_item item;
	size_t len;
	void *data;

	/* Verify invalid flags for perf config queries */
	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_PERF_CONFIG;
	item.flags = 42; /* invalid */
	i915_query_items(fd, &item, 1);
	igt_assert_eq(item.length, -EINVAL);

	/*
	 * A too small data length is invalid. We should have at least
	 * the test config list.
	 */
	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_PERF_CONFIG;
	item.flags = DRM_I915_QUERY_PERF_CONFIG_LIST;
	item.length = sizeof(struct drm_i915_query_perf_config); /* invalid */
	i915_query_items(fd, &item, 1);
	igt_assert_eq(item.length, -EINVAL);

	/* Flags on the query config data are invalid. */
	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_PERF_CONFIG;
	item.flags = DRM_I915_QUERY_PERF_CONFIG_LIST;
	item.length = 0;
	i915_query_items(fd, &item, 1);
	igt_assert(item.length > sizeof(struct drm_i915_query_perf_config));

	query_config_ptr = calloc(1, item.length);
	query_config_ptr->flags = 1; /* invalid */
	item.data_ptr = to_user_pointer(query_config_ptr);
	i915_query_items(fd, &item, 1);
	igt_assert_eq(item.length, -EINVAL);
	free(query_config_ptr);

	/*
	 * A NULL data pointer is invalid when the length is long
	 * enough for i915 to copy data into the pointed memory.
	 */
	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_PERF_CONFIG;
	item.flags = DRM_I915_QUERY_PERF_CONFIG_LIST;
	item.length = 0;
	i915_query_items(fd, &item, 1);
	igt_assert(item.length > sizeof(struct drm_i915_query_perf_config));

	i915_query_items(fd, &item, 1); /* leaves data ptr to null */
	igt_assert_eq(item.length, -EFAULT);

	/* Trying to write into read only memory will fail. */
	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_PERF_CONFIG;
	item.flags = DRM_I915_QUERY_PERF_CONFIG_LIST;
	item.length = 0;
	i915_query_items(fd, &item, 1);
	igt_assert(item.length > sizeof(struct drm_i915_query_perf_config));

	len = ALIGN(item.length, 4096);
	data = mmap(0, len, PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	memset(data, 0, len);
	mprotect(data, len, PROT_READ);
	item.data_ptr = to_user_pointer(data); /* invalid with read only data */
	i915_query_items(fd, &item, 1);
	igt_assert_eq(item.length, -EFAULT);

	munmap(data, len);
}

static int query_perf_config_data(int fd, int length,
				  struct drm_i915_query_perf_config *query)
{
	struct drm_i915_query_item item;

	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_PERF_CONFIG;
	item.flags = DRM_I915_QUERY_PERF_CONFIG_DATA;
	item.length = length;
	item.data_ptr = to_user_pointer(query);
	i915_query_items(fd, &item, 1);

	return item.length;
}

/*
 * Verify that perf configuration queries for configuration data
 * rejects invalid parameters.
 */
static void test_query_perf_config_data_invalid(int fd)
{
	struct {
		struct drm_i915_query_perf_config query;
		struct drm_i915_perf_oa_config oa;
	} query;
	struct drm_i915_query_item item;
	size_t len;
	void *data;

	/* Flags are invalid for perf config queries */
	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_PERF_CONFIG;
	item.flags = 42; /* invalid */
	i915_query_items(fd, &item, 1);
	igt_assert_eq(item.length, -EINVAL);

	/*
	 * A too small data length is invalid. We should have at least
	 * the test config list.
	 */
	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_PERF_CONFIG;
	item.flags = DRM_I915_QUERY_PERF_CONFIG_DATA;
	item.length = sizeof(struct drm_i915_query_perf_config); /* invalid */
	i915_query_items(fd, &item, 1);
	igt_assert_eq(item.length, -EINVAL);

	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_PERF_CONFIG;
	item.flags = DRM_I915_QUERY_PERF_CONFIG_DATA;
	item.length = sizeof(struct drm_i915_query_perf_config) +
		sizeof(struct drm_i915_perf_oa_config) - 1; /* invalid */
	i915_query_items(fd, &item, 1);
	igt_assert_eq(item.length, -EINVAL);

	/* Flags on the query config data are invalid. */
	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_PERF_CONFIG;
	item.flags = DRM_I915_QUERY_PERF_CONFIG_DATA;
	item.length = 0;
	i915_query_items(fd, &item, 1);
	igt_assert_eq(item.length, sizeof(query));

	memset(&query, 0, sizeof(query));
	query.query.flags = 1; /* invalid */
	item.data_ptr = to_user_pointer(&query.query);
	i915_query_items(fd, &item, 1);
	igt_assert_eq(item.length, -EINVAL);

	/*
	 * A NULL data pointer is invalid when the length is long
	 * enough for i915 to copy data into the pointed memory.
	 */
	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_PERF_CONFIG;
	item.flags = DRM_I915_QUERY_PERF_CONFIG_DATA;
	item.length = 0;
	i915_query_items(fd, &item, 1);
	igt_assert_eq(item.length, sizeof(query));

	i915_query_items(fd, &item, 1); /* leaves data ptr to null */
	igt_assert_eq(item.length, -EFAULT);

	item.data_ptr = ULONG_MAX; /* invalid pointer */
	i915_query_items(fd, &item, 1);
	igt_assert_eq(item.length, -EFAULT);

	/* Trying to write into read only memory will fail. */
	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_PERF_CONFIG;
	item.flags = DRM_I915_QUERY_PERF_CONFIG_DATA;
	item.length = 0;
	i915_query_items(fd, &item, 1);
	igt_assert_eq(item.length, sizeof(query));

	len = ALIGN(item.length, 4096);
	data = mmap(0, len, PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	memset(data, 0, len);
	((struct drm_i915_query_perf_config *)data)->config = 1; /* test config */
	mprotect(data, len, PROT_READ);
	item.data_ptr = to_user_pointer(data); /* invalid with read only data */
	i915_query_items(fd, &item, 1);
	igt_assert_eq(item.length, -EFAULT);

	munmap(data, len);

	/* Invalid memory (NULL) for configuration registers. */
	memset(&query, 0, sizeof(query));
	query.query.config = 1; /* test config */
	igt_assert_eq(sizeof(query),
		      query_perf_config_data(fd, sizeof(query), &query.query));

	igt_debug("Queried test config %.*s\n",
		  (int)sizeof(query.oa.uuid), query.oa.uuid);
	igt_debug("  n_mux_regs=%u, n_boolean_regs=%u, n_flex_regs=%u\n",
		  query.oa.n_mux_regs, query.oa.n_boolean_regs,
		  query.oa.n_flex_regs);
	igt_assert_eq(-EFAULT,
		      query_perf_config_data(fd, sizeof(query), &query.query));

	/* Invalid memory (ULONG max) for configuration registers. */
	memset(&query, 0, sizeof(query));
	query.query.config = 1; /* test config */
	igt_assert_eq(sizeof(query), query_perf_config_data(fd, 0, &query.query));

	if (query.oa.n_mux_regs > 0) {
		query.oa.mux_regs_ptr = ULONG_MAX;
		query.oa.n_boolean_regs = 0;
		query.oa.n_flex_regs = 0;
		igt_assert_eq(-EFAULT, query_perf_config_data(fd, sizeof(query),
							      &query.query));
	}

	memset(&query, 0, sizeof(query));
	query.query.config = 1; /* test config */
	igt_assert_eq(sizeof(query),
		      query_perf_config_data(fd, 0, &query.query));

	if (query.oa.n_boolean_regs > 0) {
		query.oa.boolean_regs_ptr = ULONG_MAX;
		query.oa.n_mux_regs = 0;
		query.oa.n_flex_regs = 0;
		igt_assert_eq(-EFAULT, query_perf_config_data(fd, sizeof(query),
							      &query.query));
	}

	memset(&query, 0, sizeof(query));
	query.query.config = 1; /* test config */
	igt_assert_eq(sizeof(query),
		      query_perf_config_data(fd, 0, &query.query));

	if (query.oa.n_flex_regs > 0) {
		query.oa.flex_regs_ptr = ULONG_MAX;
		query.oa.n_mux_regs = 0;
		query.oa.n_boolean_regs = 0;
		igt_assert_eq(-EFAULT, query_perf_config_data(fd, sizeof(query),
							      &query.query));
	}

	/* Too small number of registers to write. */
	memset(&query, 0, sizeof(query));
	query.query.config = 1; /* test config */
	igt_assert_eq(sizeof(query), query_perf_config_data(fd, 0, &query.query));

	if (query.oa.n_mux_regs > 0) {
		query.oa.n_mux_regs--;
		igt_assert_eq(-EINVAL, query_perf_config_data(fd, sizeof(query),
							      &query.query));
	}

	memset(&query, 0, sizeof(query));
	query.query.config = 1; /* test config */
	igt_assert_eq(sizeof(query),
		      query_perf_config_data(fd, 0, &query.query));

	if (query.oa.n_boolean_regs > 0) {
		query.oa.n_boolean_regs--;
		igt_assert_eq(-EINVAL, query_perf_config_data(fd, sizeof(query),
							      &query.query));
	}

	memset(&query, 0, sizeof(query));
	query.query.config = 1; /* test config */
	igt_assert_eq(sizeof(query), query_perf_config_data(fd, 0, &query.query));

	if (query.oa.n_flex_regs > 0) {
		query.oa.n_flex_regs--;
		igt_assert_eq(-EINVAL, query_perf_config_data(fd, sizeof(query),
							      &query.query));
	}

	/* Read only memory for registers. */
	memset(&query, 0, sizeof(query));
	query.query.config = 1; /* test config */
	igt_assert_eq(sizeof(query),
		      query_perf_config_data(fd, sizeof(query), &query.query));

	len = ALIGN(query.oa.n_mux_regs * sizeof(uint32_t) * 2, 4096);
	data = mmap(0, len, PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	memset(data, 0, len);
	mprotect(data, len, PROT_READ);
	query.oa.mux_regs_ptr = to_user_pointer(data);
	igt_assert_eq(-EFAULT,
		      query_perf_config_data(fd, sizeof(query), &query.query));

	munmap(data, len);
}

static uint64_t create_perf_config(int fd,
				   const char *uuid,
				   uint32_t **boolean_regs,
				   uint32_t *n_boolean_regs,
				   uint32_t **flex_regs,
				   uint32_t *n_flex_regs,
				   uint32_t **mux_regs,
				   uint32_t *n_mux_regs)
{
	struct drm_i915_perf_oa_config config;
	int devid = intel_get_drm_devid(fd);
	int i, ret;

	*n_boolean_regs = rand() % 50;
	*boolean_regs = calloc(*n_boolean_regs, sizeof(uint32_t) * 2);
	*n_mux_regs = rand() % 50;
	*mux_regs = calloc(*n_mux_regs, sizeof(uint32_t) * 2);
	if (intel_gen(devid) < 8) {
		/* flex register don't exist on gen7 */
		*n_flex_regs = 0;
		*flex_regs = NULL;
	} else {
		*n_flex_regs = rand() % 50;
		*flex_regs = calloc(*n_flex_regs, sizeof(uint32_t) * 2);
	}

	for (i = 0; i < *n_boolean_regs; i++) {
		if (rand() % 2) {
			/* OASTARTTRIG[1-8] */
			(*boolean_regs)[i * 2] =
				0x2710 + ((rand() % (0x2730 - 0x2710)) / 4) * 4;
			(*boolean_regs)[i * 2 + 1] = rand();
		} else {
			/* OAREPORTTRIG[1-8] */
			(*boolean_regs)[i * 2] =
				0x2740 + ((rand() % (0x275c - 0x2744)) / 4) * 4;
			(*boolean_regs)[i * 2 + 1] = rand();
		}
	}

	for (i = 0; i < *n_mux_regs; i++) {
		(*mux_regs)[i * 2] = 0x9800;
		(*mux_regs)[i * 2 + 1] = rand();
	}

	for (i = 0; i < *n_flex_regs; i++) {
		const uint32_t flex[] = {
			0xe458,
			0xe558,
			0xe658,
			0xe758,
			0xe45c,
			0xe55c,
			0xe65c
		};
		(*flex_regs)[i * 2] = flex[rand() % ARRAY_SIZE(flex)];
		(*flex_regs)[i * 2 + 1] = rand();
	}

	memset(&config, 0, sizeof(config));
	memcpy(config.uuid, uuid, sizeof(config.uuid));

	config.n_boolean_regs = *n_boolean_regs;
	config.boolean_regs_ptr = to_user_pointer(*boolean_regs);
	config.n_flex_regs = *n_flex_regs;
	config.flex_regs_ptr = to_user_pointer(*flex_regs);
	config.n_mux_regs = *n_mux_regs;
	config.mux_regs_ptr = to_user_pointer(*mux_regs);

	ret = igt_ioctl(fd, DRM_IOCTL_I915_PERF_ADD_CONFIG, &config);
	igt_assert(ret > 1); /* Config 0/1 should be used by the kernel */

	igt_debug("created config id=%i uuid=%s:\n", ret, uuid);
	igt_debug("\tn_boolean_regs=%u n_flex_regs=%u n_mux_regs=%u\n",
		  config.n_boolean_regs, config.n_flex_regs,
		  config.n_mux_regs);

	return ret;
}

static void remove_perf_config(int fd, uint64_t config_id)
{
	igt_assert_eq(0, igt_ioctl(fd, DRM_IOCTL_I915_PERF_REMOVE_CONFIG,
				   &config_id));
}

static uint64_t get_config_id(int fd, const char *uuid)
{
	char rel_path[100];
	uint64_t ret;
	int sysfs;

	sysfs = igt_sysfs_open(fd, NULL);
	igt_assert_lte(0, sysfs);

	snprintf(rel_path, sizeof(rel_path), "metrics/%s/id", uuid);

	if (igt_sysfs_scanf(sysfs, rel_path, "%lu", &ret) < 0)
		ret = 0;

	close(sysfs);
	return ret;
}

/*
 * Verifies that created configurations appear in the query of list of
 * configuration and also verify the content of the queried
 * configurations matches with what was created.
 */
static void test_query_perf_configs(int fd)
{
	struct {
		uint64_t id;

		char uuid[40];

		uint32_t *boolean_regs;
		uint32_t n_boolean_regs;
		uint32_t *flex_regs;
		uint32_t n_flex_regs;
		uint32_t *mux_regs;
		uint32_t n_mux_regs;
	} configs[5];
	struct {
		struct drm_i915_query_perf_config query;
		uint64_t config_ids[];
	} *list_query;
	struct drm_i915_query_item item;
	int i;

	srand(time(NULL));

	for (i = 0; i < ARRAY_SIZE(configs); i++) {
		uint64_t prev_config_id;

		snprintf(configs[i].uuid, sizeof(configs[i].uuid),
			 "01234567-%04u-0123-0123-0123456789ab", i);

		prev_config_id = get_config_id(fd, configs[i].uuid);
		if (prev_config_id)
			remove_perf_config(fd, prev_config_id);

		configs[i].id =
			create_perf_config(fd, configs[i].uuid,
					   &configs[i].boolean_regs,
					   &configs[i].n_boolean_regs,
					   &configs[i].flex_regs,
					   &configs[i].n_flex_regs,
					   &configs[i].mux_regs,
					   &configs[i].n_mux_regs);
	}

	memset(&item, 0, sizeof(item));
	item.query_id = DRM_I915_QUERY_PERF_CONFIG;
	item.flags = DRM_I915_QUERY_PERF_CONFIG_LIST;
	item.length = 0;
	i915_query_items(fd, &item, 1);
	igt_assert(item.length > sizeof(struct drm_i915_query_perf_config));

	list_query = malloc(item.length);
	memset(list_query, 0, item.length);
	item.data_ptr = to_user_pointer(list_query);
	i915_query_items(fd, &item, 1);
	igt_assert(item.length > sizeof(struct drm_i915_query_perf_config));

	igt_debug("listed configs:\n");
	for (i = 0; i < list_query->query.config; i++)
		igt_debug("\tid=%lu\n", list_query->config_ids[i]);

	/* Verify that all created configs are listed. */
	for (i = 0; i < ARRAY_SIZE(configs); i++) {
		int j;
		bool found = false;

		for (j = 0; j < list_query->query.config; j++) {
			if (list_query->config_ids[j] == configs[i].id) {
				found = true;
				break;
			}
		}

		igt_assert(found);
	}

	/* Verify the content of the configs. */
	for (i = 0; i < ARRAY_SIZE(configs); i++) {
		struct {
			struct drm_i915_query_perf_config query;
			struct drm_i915_perf_oa_config oa;
		} query;
		uint32_t *boolean_regs = NULL, *flex_regs = NULL, *mux_regs = NULL;

		memset(&query, 0, sizeof(query));
		query.query.config = configs[i].id;
		igt_assert_eq(sizeof(query),
			      query_perf_config_data(fd, sizeof(query),
						     &query.query));

		igt_debug("queried config data id=%lu uuid=%s:\n",
			  configs[i].id, configs[i].uuid);
		igt_debug("\tn_boolean_regs=%u n_flex_regs=%u n_mux_regs=%u\n",
			  query.oa.n_boolean_regs, query.oa.n_flex_regs,
			  query.oa.n_mux_regs);

		igt_assert_eq(query.oa.n_boolean_regs, configs[i].n_boolean_regs);
		igt_assert_eq(query.oa.n_flex_regs, configs[i].n_flex_regs);
		igt_assert_eq(query.oa.n_mux_regs, configs[i].n_mux_regs);

		boolean_regs = calloc(query.oa.n_boolean_regs * 2, sizeof(uint32_t));
		if (query.oa.n_flex_regs > 0)
			flex_regs = calloc(query.oa.n_flex_regs * 2, sizeof(uint32_t));
		mux_regs = calloc(query.oa.n_mux_regs * 2, sizeof(uint32_t));

		query.oa.boolean_regs_ptr = to_user_pointer(boolean_regs);
		query.oa.flex_regs_ptr = to_user_pointer(flex_regs);
		query.oa.mux_regs_ptr = to_user_pointer(mux_regs);

		igt_assert_eq(sizeof(query),
			      query_perf_config_data(fd, sizeof(query),
						     &query.query));

		igt_assert_eq(0, memcmp(configs[i].boolean_regs,
					boolean_regs,
					configs[i].n_boolean_regs * 2 * sizeof(uint32_t)));
		igt_assert_eq(0, memcmp(configs[i].flex_regs,
					flex_regs,
					configs[i].n_flex_regs * 2 * sizeof(uint32_t)));
		igt_assert_eq(0, memcmp(configs[i].mux_regs,
					mux_regs,
					configs[i].n_mux_regs * 2 * sizeof(uint32_t)));

		free(boolean_regs);
		free(flex_regs);
		free(mux_regs);
	}

	for (i = 0; i < ARRAY_SIZE(configs); i++) {
		remove_perf_config(fd, configs[i].id);

		free(configs[i].boolean_regs);
		free(configs[i].flex_regs);
		free(configs[i].mux_regs);
	}
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

	igt_subtest("query-garbage-items") {
		igt_require(query_topology_supported(fd));
		test_query_garbage_items(fd);
	}

	igt_subtest("query-topology-kernel-writes") {
		igt_require(query_topology_supported(fd));
		test_query_topology_kernel_writes(fd);
	}

	igt_subtest("query-topology-unsupported") {
		igt_require(!query_topology_supported(fd));
		test_query_topology_unsupported(fd);
	}

	igt_subtest("query-topology-coherent-slice-mask") {
		igt_require(query_topology_supported(fd));
		test_query_topology_coherent_slice_mask(fd);
	}

	igt_subtest("query-topology-matches-eu-total") {
		igt_require(query_topology_supported(fd));
		test_query_topology_matches_eu_total(fd);
	}

	igt_subtest("query-topology-known-pci-ids") {
		igt_require(query_topology_supported(fd));
		igt_require(IS_HASWELL(devid) || IS_BROADWELL(devid) ||
			    IS_SKYLAKE(devid) || IS_KABYLAKE(devid) ||
			    IS_COFFEELAKE(devid));
		test_query_topology_known_pci_ids(fd, devid);
	}

	igt_subtest("query-perf-config-list-invalid") {
		igt_require(query_perf_config_supported(fd));
		test_query_perf_config_list_invalid(fd);
	}

	igt_subtest("query-perf-config-data-invalid") {
		igt_require(query_perf_config_supported(fd));
		test_query_perf_config_data_invalid(fd);
	}

	igt_subtest("query-perf-configs") {
		igt_require(query_perf_config_supported(fd));
		test_query_perf_configs(fd);
	}

	igt_fixture {
		close(fd);
	}
}
