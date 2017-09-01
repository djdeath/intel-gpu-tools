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
 *
 * Authors:
 *    Lionel Landwerlin <lionel.g.landwerlin@intel.com>
 *
 */

#include "igt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <sys/wait.h>

#include "igt_sysfs.h"
#include "ioctl_wrappers.h"

IGT_TEST_DESCRIPTION("Test context render powergating programming.");

#define MI_STORE_REGISTER_MEM (0x24 << 23)

#define MI_SET_PREDICATE      (0x1 << 23)
#define  MI_SET_PREDICATE_NOOP_NEVER         (0)
#define  MI_SET_PREDICATE_NOOP_RESULT2_CLEAR (1)
#define  MI_SET_PREDICATE_NOOP_RESULT2_SET   (2)
#define  MI_SET_PREDICATE_NOOP_RESULT_CLEAR  (3)
#define  MI_SET_PREDICATE_NOOP_RESULT_SET    (4)
#define  MI_SET_PREDICATE_1_SLICES           (5)
#define  MI_SET_PREDICATE_2_SLICES           (6)
#define  MI_SET_PREDICATE_3_SLICES           (7)

#define GEN8_R_PWR_CLK_STATE		0x20C8
#define   GEN8_RPCS_ENABLE		(1 << 31)
#define   GEN8_RPCS_S_CNT_ENABLE	(1 << 18)
#define   GEN8_RPCS_S_CNT_SHIFT		15
#define   GEN8_RPCS_S_CNT_MASK		(0x7 << GEN8_RPCS_S_CNT_SHIFT)
#define   GEN8_RPCS_SS_CNT_ENABLE	(1 << 11)
#define   GEN8_RPCS_SS_CNT_SHIFT	8
#define   GEN8_RPCS_SS_CNT_MASK		(0x7 << GEN8_RPCS_SS_CNT_SHIFT)
#define   GEN8_RPCS_EU_MAX_SHIFT	4
#define   GEN8_RPCS_EU_MAX_MASK		(0xf << GEN8_RPCS_EU_MAX_SHIFT)
#define   GEN8_RPCS_EU_MIN_SHIFT	0
#define   GEN8_RPCS_EU_MIN_MASK		(0xf << GEN8_RPCS_EU_MIN_SHIFT)

#define RCS_TIMESTAMP (0x2000 + 0x358)

static int drm_fd;
static int devid;
static uint64_t device_slice_mask = 0;
static uint64_t device_subslice_mask = 0;
static uint32_t device_slice_count = 0;
static uint32_t device_subslice_count = 0;

static uint64_t mask_minus_one(uint64_t mask)
{
	int i;

	for (i = 0; i < (sizeof(mask) * 8 - 1); i++) {
		if ((1UL << i) & mask) {
			return mask & ~(1UL << i);
		}
	}

	igt_assert(!"reached");
	return 0;
}

static uint64_t mask_plus_one(uint64_t mask)
{
	int i;

	for (i = 0; i < (sizeof(mask) * 8 - 1); i++) {
		if (((1UL << i) & mask) == 0) {
			return mask | (1UL << i);
		}
	}

	igt_assert(!"reached");
	return 0;
}

static uint64_t mask_minus(uint64_t mask, int n)
{
	int i;

	for (i = 0; i < n; i++)
		mask = mask_minus_one(mask);

	return mask;
}

static uint64_t mask_plus(uint64_t mask, int n)
{
	int i;

	for (i = 0; i < n; i++)
		mask = mask_plus_one(mask);

	return mask;
}

static uint32_t *
fill_relocation(uint32_t *batch,
		struct drm_i915_gem_relocation_entry *reloc,
		uint32_t gem_handle, uint32_t delta, /* in bytes */
		uint32_t offset, /* in dwords */
		uint32_t read_domains, uint32_t write_domains)
{
	reloc->target_handle = gem_handle;
	reloc->delta = delta;
	reloc->offset = offset * sizeof(uint32_t);
	reloc->presumed_offset = 0;
	reloc->read_domains = read_domains;
	reloc->write_domain = write_domains;

	*batch++ = delta;
	*batch++ = 0;

	return batch;
}


static uint32_t
read_rpcs_reg(uint32_t context,
	      uint32_t expected_slices)
{
	struct drm_i915_gem_execbuffer2 execbuf;
	struct drm_i915_gem_exec_object2 obj[2];
	struct drm_i915_gem_relocation_entry relocs[2];
	uint32_t *batch, *b, data[2];
	uint32_t rpcs;
	int n_relocs = 0;

	memset(obj, 0, sizeof(obj));
	obj[0].handle = gem_create(drm_fd, 4096);
	obj[1].handle = gem_create(drm_fd, 4096);

	batch = b = gem_mmap__cpu(drm_fd, obj[1].handle, 0, 4096,
				  PROT_READ | PROT_WRITE);

	if (expected_slices != 0 && intel_gen(devid) < 10) {
		*b++ = MI_SET_PREDICATE | (1 - 1) |
			(MI_SET_PREDICATE_1_SLICES + expected_slices - 1);
	}

	*b++ = MI_STORE_REGISTER_MEM | (4 - 2);
	*b++ = RCS_TIMESTAMP;
	b = fill_relocation(b, &relocs[n_relocs++], obj[0].handle,
			    0, b - batch,
			    I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER);

	*b++ = MI_STORE_REGISTER_MEM | (4 - 2);
	*b++ = GEN8_R_PWR_CLK_STATE;
	b = fill_relocation(b, &relocs[n_relocs++], obj[0].handle,
			    4, b - batch,
			    I915_GEM_DOMAIN_RENDER, I915_GEM_DOMAIN_RENDER);

	if (expected_slices != 0 && intel_gen(devid) < 10)
		*b++ = MI_SET_PREDICATE | (1 - 1) | MI_SET_PREDICATE_NOOP_NEVER;

	*b++ = MI_BATCH_BUFFER_END;

	gem_munmap(batch, 4096);

	obj[1].relocation_count = n_relocs;
	obj[1].relocs_ptr = to_user_pointer(relocs);

	memset(&execbuf, 0, sizeof(execbuf));
	execbuf.buffers_ptr = to_user_pointer(obj);
	execbuf.buffer_count = ARRAY_SIZE(obj);
	i915_execbuffer2_set_context_id(execbuf, context);

	gem_execbuf(drm_fd, &execbuf);

	gem_read(drm_fd, obj[0].handle, 0, data, sizeof(data));

	rpcs = data[1];

	igt_debug("rcs_timestamp=0x%x rpcs=0x%x/0x%x\n", data[0], data[1], ((data[1] & GEN8_RPCS_S_CNT_MASK) >> GEN8_RPCS_S_CNT_SHIFT));

	gem_close(drm_fd, obj[0].handle);
	gem_close(drm_fd, obj[1].handle);

	return rpcs;
}

static uint32_t
read_slice_count(uint32_t context,
		 uint32_t expected_slice_count)
{
	return (read_rpcs_reg(context, expected_slice_count) & GEN8_RPCS_S_CNT_MASK)
		>> GEN8_RPCS_S_CNT_SHIFT;
}

static uint32_t
read_subslice_count(uint32_t context)
{
	return (read_rpcs_reg(context, 0) & GEN8_RPCS_SS_CNT_MASK)
		>> GEN8_RPCS_SS_CNT_SHIFT;
}

static bool
kernel_has_per_context_sseu_support(void)
{
	struct drm_i915_gem_context_param arg;
	struct drm_i915_gem_context_param_sseu sseu;

	memset(&sseu, 0, sizeof(sseu));
	sseu.class = 0; /* rcs */
	sseu.instance = 0;

	memset(&arg, 0, sizeof(arg));
	arg.ctx_id = 0; /* default context */
	arg.param = I915_CONTEXT_PARAM_SSEU;
	arg.value = to_user_pointer(&sseu);

	if (__gem_context_get_param(drm_fd, &arg))
		return false;

	return true;
}

static bool
platform_has_per_context_sseu_support(void)
{
	struct drm_i915_gem_context_param arg;
	struct drm_i915_gem_context_param_sseu sseu;
	int ret;

	memset(&sseu, 0, sizeof(sseu));
	sseu.class = 0; /* rcs */
	sseu.instance = 0;

	memset(&arg, 0, sizeof(arg));
	arg.ctx_id = 0; /* default context */
	arg.param = I915_CONTEXT_PARAM_SSEU;
	arg.value = to_user_pointer(&sseu);

	ret = __gem_context_get_param(drm_fd, &arg);
	igt_assert(ret == 0 || errno == EINVAL);
	if (ret)
		return false;

	ret = __gem_context_set_param(drm_fd, &arg);
	igt_assert(ret == 0 || errno == ENODEV);
	if (ret)
		return false;

	return true;
}

static void
context_get_sseu_masks(uint32_t context,
		       uint64_t *slice_mask,
		       uint64_t *subslice_mask)
{
	struct drm_i915_gem_context_param arg;
	struct drm_i915_gem_context_param_sseu sseu;

	memset(&sseu, 0, sizeof(sseu));
	sseu.class = 0; /* rcs */
	sseu.instance = 0;

	memset(&arg, 0, sizeof(arg));
	arg.ctx_id = context;
	arg.param = I915_CONTEXT_PARAM_SSEU;
	arg.value = to_user_pointer(&sseu);

	gem_context_get_param(drm_fd, &arg);

	if (slice_mask)
		*slice_mask = sseu.slice_mask;
	if (subslice_mask)
		*subslice_mask = sseu.subslice_mask;
}

static void
context_set_slice_mask(uint32_t context, uint64_t slice_mask)
{
	struct drm_i915_gem_context_param arg;
	struct drm_i915_gem_context_param_sseu sseu;

	memset(&sseu, 0, sizeof(sseu));
	sseu.class = 0; /* rcs */
	sseu.instance = 0;

	memset(&arg, 0, sizeof(arg));
	arg.ctx_id = context;
	arg.param = I915_CONTEXT_PARAM_SSEU;
	arg.value = to_user_pointer(&sseu);

	gem_context_get_param(drm_fd, &arg);

	sseu.slice_mask = slice_mask;

	gem_context_set_param(drm_fd, &arg);
}

static void
context_set_subslice_mask(uint32_t context, uint64_t subslice_mask)
{
	struct drm_i915_gem_context_param arg;
	struct drm_i915_gem_context_param_sseu sseu;

	memset(&sseu, 0, sizeof(sseu));
	sseu.class = 0; /* rcs */
	sseu.instance = 0;

	memset(&arg, 0, sizeof(arg));
	arg.ctx_id = context;
	arg.param = I915_CONTEXT_PARAM_SSEU;
	arg.value = to_user_pointer(&sseu);

	gem_context_get_param(drm_fd, &arg);

	sseu.subslice_mask = subslice_mask;

	gem_context_set_param(drm_fd, &arg);
}

/*
 * Verify that we can program the slice count.
 */
static void
test_sseu_slice_program_gt(uint32_t pg_slice_count)
{
	uint32_t pg_contexts[2], df_contexts[2];
	uint64_t pg_slice_mask = mask_minus(device_slice_mask, pg_slice_count);
	uint32_t slice_count = __builtin_popcount(pg_slice_mask);
	uint64_t slice_mask;
	int i;

	igt_debug("Running with %i slices powergated\n", pg_slice_count);

	for (i = 0; i < ARRAY_SIZE(pg_contexts); i++) {
		pg_contexts[i] = gem_context_create(drm_fd);
		df_contexts[i] = gem_context_create(drm_fd);

		context_set_slice_mask(pg_contexts[i], pg_slice_mask);
		context_set_slice_mask(df_contexts[i], device_slice_mask);
	}

	for (int i = 0; i < ARRAY_SIZE(pg_contexts); i++) {
		context_get_sseu_masks(pg_contexts[i], &slice_mask, NULL);
		igt_assert_eq(pg_slice_mask, slice_mask);
	}

	for (int i = 0; i < ARRAY_SIZE(df_contexts); i++) {
		context_get_sseu_masks(df_contexts[i], &slice_mask, NULL);
		igt_assert_eq(device_slice_mask, slice_mask);
	}

	/*
	 * Test false positives with predicates (only available on
	 * before Gen10).
	 */
	if (intel_gen(devid) < 10) {
		igt_assert_eq(0, read_slice_count(pg_contexts[0],
						  device_slice_count));
	}

	igt_debug("pg_contexts:\n");
	igt_assert_eq(slice_count, read_slice_count(pg_contexts[0], 0));
	igt_assert_eq(slice_count, read_slice_count(pg_contexts[1], 0));
	igt_assert_eq(slice_count, read_slice_count(pg_contexts[0], 0));
	igt_assert_eq(slice_count, read_slice_count(pg_contexts[0], 0));

	igt_debug("df_contexts:\n");
	igt_assert_eq(device_slice_count, read_slice_count(df_contexts[0], 0));
	igt_assert_eq(device_slice_count, read_slice_count(df_contexts[1], 0));
	igt_assert_eq(device_slice_count, read_slice_count(df_contexts[0], 0));
	igt_assert_eq(device_slice_count, read_slice_count(df_contexts[0], 0));

	igt_debug("mixed:\n");
	igt_assert_eq(slice_count, read_slice_count(pg_contexts[0], 0));

	igt_assert_eq(device_slice_count, read_slice_count(df_contexts[0], 0));


	for (int i = 0; i < ARRAY_SIZE(pg_contexts); i++) {
		gem_context_destroy(drm_fd, pg_contexts[i]);
		gem_context_destroy(drm_fd, df_contexts[i]);
	}
}

/*
 * Verify that we can program the subslice count.
 */
static void
test_sseu_subslice_program_gt(int pg_subslice_count)
{
	uint64_t pg_subslice_mask =
		mask_minus(device_subslice_mask, pg_subslice_count);
	uint32_t subslice_count = __builtin_popcount(pg_subslice_mask);
	uint64_t subslice_mask;
	uint32_t context1, context2;

	igt_debug("Running with %i subslices powergated\n", pg_subslice_count);

	context1 = gem_context_create(drm_fd);
	context2 = gem_context_create(drm_fd);

	context_set_subslice_mask(context1, pg_subslice_mask);
	context_set_subslice_mask(context2, device_subslice_mask);

	context_get_sseu_masks(context1, NULL, &subslice_mask);
	igt_assert_eq(pg_subslice_mask, subslice_mask);
	context_get_sseu_masks(context2, NULL, &subslice_mask);
	igt_assert_eq(device_subslice_mask, subslice_mask);

	igt_assert_eq(subslice_count, read_subslice_count(context1));
	igt_assert_eq(device_subslice_count, read_subslice_count(context2));

	context_set_subslice_mask(context1, device_subslice_mask);
	context_set_subslice_mask(context2, pg_subslice_mask);

	context_get_sseu_masks(context1, NULL, &subslice_mask);
	igt_assert_eq(device_subslice_mask, subslice_mask);
	context_get_sseu_masks(context2, NULL, &subslice_mask);
	igt_assert_eq(pg_subslice_mask, subslice_mask);

	igt_assert_eq(device_subslice_count, read_subslice_count(context1));
	igt_assert_eq(subslice_count, read_subslice_count(context2));

	gem_context_destroy(drm_fd, context1);
	gem_context_destroy(drm_fd, context2);
}

/*
 * Verify that invalid engine class/instance is properly rejected.
 */
static void
test_sseu_invalid_engine(void)
{
	struct drm_i915_gem_context_param arg;
	struct drm_i915_gem_context_param_sseu sseu;

	memset(&sseu, 0, sizeof(sseu));

	memset(&arg, 0, sizeof(arg));
	arg.ctx_id = 0; /* default context */
	arg.param = I915_CONTEXT_PARAM_SSEU;
	arg.value = to_user_pointer(&sseu);

	sseu.class = I915_ENGINE_CLASS_VIDEO_ENHANCE + 1; /* invalid */
	sseu.instance = 0;
	igt_assert_eq(-EINVAL, __gem_context_get_param(drm_fd, &arg));

	sseu.class = 0;
	sseu.instance = 0xffff; /* assumed invalid */
	igt_assert_eq(-EINVAL, __gem_context_get_param(drm_fd, &arg));

	/*
	 * Get some proper values before trying to reprogram them onto
	 * an invalid engine.
	 */
	sseu.class = 0;
	sseu.instance = 0;
	gem_context_get_param(drm_fd, &arg);

	sseu.class = I915_ENGINE_CLASS_VIDEO_ENHANCE + 2; /* invalid */
	sseu.instance = 0;
	igt_assert_eq(-EINVAL, __gem_context_get_param(drm_fd, &arg));

	sseu.class = 0;
	sseu.instance = 0xffff; /* assumed invalid */
	igt_assert_eq(-EINVAL, __gem_context_get_param(drm_fd, &arg));
}

/*
 * Verify that invalid values are rejected.
 */
static void
test_sseu_invalid_values(void)
{
	struct drm_i915_gem_context_param arg;
	struct drm_i915_gem_context_param_sseu default_sseu, sseu;
	int i;

	memset(&default_sseu, 0, sizeof(default_sseu));
	default_sseu.class = 0; /* rcs */
	default_sseu.instance = 0;

	memset(&arg, 0, sizeof(arg));
	arg.ctx_id = 0; /* default context */
	arg.param = I915_CONTEXT_PARAM_SSEU;
	arg.value = to_user_pointer(&default_sseu);

	gem_context_get_param(drm_fd, &arg);

	arg.value = to_user_pointer(&sseu);

        /* Try non 0 rsvd fields. */
	sseu = default_sseu;
	sseu.rsvd1 = 1;
	igt_assert_eq(-EINVAL, __gem_context_get_param(drm_fd, &arg));
	igt_assert_eq(-EINVAL, __gem_context_set_param(drm_fd, &arg));

	sseu = default_sseu;
	sseu.rsvd1 = 0xff00ff00;
	igt_assert_eq(-EINVAL, __gem_context_get_param(drm_fd, &arg));
	igt_assert_eq(-EINVAL, __gem_context_set_param(drm_fd, &arg));

	sseu = default_sseu;
	sseu.rsvd2 = 1;
	igt_assert_eq(-EINVAL, __gem_context_get_param(drm_fd, &arg));
	igt_assert_eq(-EINVAL, __gem_context_set_param(drm_fd, &arg));

	sseu = default_sseu;
	sseu.rsvd2 = 0xff00ff00;
	igt_assert_eq(-EINVAL, __gem_context_get_param(drm_fd, &arg));
	igt_assert_eq(-EINVAL, __gem_context_set_param(drm_fd, &arg));

	sseu = default_sseu;
	sseu.rsvd1 = 42;
	sseu.rsvd2 = 42 * 42;
	igt_assert_eq(-EINVAL, __gem_context_get_param(drm_fd, &arg));
	igt_assert_eq(-EINVAL, __gem_context_set_param(drm_fd, &arg));

	/* Try all slice masks known to be invalid. */
	sseu = default_sseu;
	for (i = 1; i <= (8 - device_slice_count); i++) {
		sseu.slice_mask = mask_plus(device_slice_mask, i);
		igt_assert_eq(-EINVAL, __gem_context_set_param(drm_fd, &arg));
	}

	/* 0 slices. */
	sseu.slice_mask = 0;
	igt_assert_eq(-EINVAL, __gem_context_set_param(drm_fd, &arg));

	/* Try all subslice masks known to be invalid. */
	sseu = default_sseu;
	for (i = 1; i <= (8 - device_subslice_count); i++) {
		sseu.subslice_mask = mask_plus(device_subslice_mask, i);
		igt_assert_eq(-EINVAL, __gem_context_set_param(drm_fd, &arg));
	}

	/* 0 subslices. */
	sseu.subslice_mask = 0;
	igt_assert_eq(-EINVAL, __gem_context_set_param(drm_fd, &arg));

	/* Try number of EUs superior to the max available. */
	sseu = default_sseu;
	sseu.min_eus_per_subslice = default_sseu.max_eus_per_subslice + 1;
	igt_assert_eq(-EINVAL, __gem_context_set_param(drm_fd, &arg));

	sseu = default_sseu;
	sseu.max_eus_per_subslice = default_sseu.max_eus_per_subslice + 1;
	igt_assert_eq(-EINVAL, __gem_context_set_param(drm_fd, &arg));

	/* Try to program 0 max EUs. */
	sseu = default_sseu;
	sseu.max_eus_per_subslice = 0;
	igt_assert_eq(-EINVAL, __gem_context_set_param(drm_fd, &arg));
}

/* Verify that the kernel returns a correct error value on Gen < 8. */
static void
init_contexts(uint32_t *contexts,
	      int n_contexts,
	      uint64_t device_slice_mask,
	      uint64_t pg_slice_mask)
{
	int i;

	for (i = 0; i < n_contexts; i++)
		contexts[i] = gem_context_create(drm_fd);

	context_set_slice_mask(contexts[0], device_slice_mask);
	context_set_slice_mask(contexts[1], pg_slice_mask);
}

/*
 * Verify that powergating settings are put on hold while i915/perf is
 * active.
 */
static void
test_sseu_perf(void)
{
	uint64_t properties[] = {
		/* Include OA reports in samples */
		DRM_I915_PERF_PROP_SAMPLE_OA, true,

		/* OA unit configuration */
		DRM_I915_PERF_PROP_OA_METRICS_SET, 1, /* test metric */
		DRM_I915_PERF_PROP_OA_FORMAT, I915_OA_FORMAT_A32u40_A4u32_B8_C8,
		DRM_I915_PERF_PROP_OA_EXPONENT, 20,
	};
	struct drm_i915_perf_open_param param = {
		.flags = I915_PERF_FLAG_FD_CLOEXEC |
		I915_PERF_FLAG_FD_NONBLOCK,
		.num_properties = ARRAY_SIZE(properties) / 2,
		.properties_ptr = to_user_pointer(properties),
	};
	uint64_t pg_slice_mask = mask_minus(device_slice_mask, 1);
	uint32_t slice_count = __builtin_popcount(pg_slice_mask);
	uint32_t contexts[2];
	int i, perf_fd;

	init_contexts(contexts, 2, device_slice_mask, pg_slice_mask);

	/*
	 * Test false positives with predicates (only available on
	 * before Gen10).
	 */
	if (intel_gen(devid) < 10) {
		igt_assert_eq(0, read_slice_count(contexts[1],
						  device_slice_count));
	}
	igt_assert_eq(device_slice_count, read_slice_count(contexts[0], 0));
	igt_assert_eq(slice_count, read_slice_count(contexts[1], 0));

	/*
	 * Now open i915/perf and verify that all contexts have been
	 * reconfigured to the device's default.
	 */
	perf_fd = igt_ioctl(drm_fd, DRM_IOCTL_I915_PERF_OPEN, &param);
	igt_assert(perf_fd >= 0);

	if (intel_gen(devid) < 10) {
		igt_assert_eq(0, read_slice_count(contexts[1], slice_count));
	}
	igt_assert_eq(device_slice_count, read_slice_count(contexts[0], 0));
	igt_assert_eq(device_slice_count, read_slice_count(contexts[1], 0));

	close(perf_fd);

	/*
	 * After closing the perf stream, configurations should be
	 * back to the programmed values.
	 */
	if (intel_gen(devid) < 10) {
		igt_assert_eq(0, read_slice_count(contexts[1],
						  device_slice_count));
	}
	igt_assert_eq(device_slice_count, read_slice_count(contexts[0], 0));
	igt_assert_eq(slice_count, read_slice_count(contexts[1], 0));

	for (i = 0; i < ARRAY_SIZE(contexts); i++)
		gem_context_destroy(drm_fd, contexts[i]);

	/*
	 * Open i915/perf first and verify that all contexts created
	 * afterward are reconfigured to the device's default.
	 */
	perf_fd = igt_ioctl(drm_fd, DRM_IOCTL_I915_PERF_OPEN, &param);
	igt_assert(perf_fd >= 0);

	init_contexts(contexts, 2, device_slice_mask, pg_slice_mask);

	/*
	 * Check the device's default values, despite setting
	 * otherwise.
	 */
	if (intel_gen(devid) < 10) {
		igt_assert_eq(0, read_slice_count(contexts[1],
						  slice_count));
	}
	igt_assert_eq(device_slice_count, read_slice_count(contexts[0], 0));
	igt_assert_eq(device_slice_count, read_slice_count(contexts[1], 0));

	close(perf_fd);

	/*
	 * After closing the perf stream, configurations should be
	 * back to the programmed values.
	 */
	if (intel_gen(devid) < 10) {
		igt_assert_eq(0, read_slice_count(contexts[1],
						  device_slice_count));
	}
	igt_assert_eq(device_slice_count, read_slice_count(contexts[0], 0));
	igt_assert_eq(slice_count, read_slice_count(contexts[1], 0));

	for (i = 0; i < ARRAY_SIZE(contexts); i++)
		gem_context_destroy(drm_fd, contexts[i]);
}

static bool get_allow_dynamic_sseu(int fd)
{
	int sysfs;
	bool ret;

	sysfs = igt_sysfs_open(fd, NULL);
	igt_assert_lte(0, sysfs);

	ret = igt_sysfs_get_boolean(sysfs, "allow_dynamic_sseu");

	close(sysfs);
	return ret;
}

static void set_allow_dynamic_sseu(int fd, bool allowed)
{
	int sysfs;

	sysfs = igt_sysfs_open(fd, NULL);
	igt_assert_lte(0, sysfs);

	igt_assert_eq(true,
                      igt_sysfs_set_boolean(sysfs,
                                            "allow_dynamic_sseu",
                                            allowed));

	close(sysfs);
}

/*
 * Verify that powergating settings are put on hold while i915/perf is
 * active.
 */
static void
test_dynamic_sseu(void)
{
	uint64_t pg_slice_mask = mask_minus(device_slice_mask, 1);
	uint64_t pg_slice_count = __builtin_popcount(pg_slice_mask);
	struct drm_i915_gem_context_param arg;
	struct drm_i915_gem_context_param_sseu sseu;

	set_allow_dynamic_sseu(drm_fd, true);

	memset(&sseu, 0, sizeof(sseu));
	sseu.class = 0; /* rcs */
	sseu.instance = 0;

	memset(&arg, 0, sizeof(arg));
	arg.ctx_id = 0; /* default context */
	arg.param = I915_CONTEXT_PARAM_SSEU;
	arg.value = to_user_pointer(&sseu);

	gem_context_get_param(drm_fd, &arg);

	/* First set the default mask */
	sseu.slice_mask = device_slice_mask;
	gem_context_set_param(drm_fd, &arg);

	igt_assert_eq(device_slice_count, read_slice_count(0, 0));

	/* Then set a powergated configuration */
	sseu.slice_mask = pg_slice_mask;
	gem_context_set_param(drm_fd, &arg);

	igt_assert_eq(pg_slice_count, read_slice_count(0, 0));

	/*
	 * Now turn off dynamic sseu and verify we get the default
	 * again
	 */
	set_allow_dynamic_sseu(drm_fd, false);

	igt_assert_eq(device_slice_count, read_slice_count(0, 0));

	gem_context_get_param(drm_fd, &arg);

	igt_assert_eq(sseu.slice_mask, pg_slice_mask);

	/* Put the device's default back again */
	sseu.slice_mask = device_slice_mask;
	gem_context_set_param(drm_fd, &arg);

	igt_assert_eq(device_slice_count, read_slice_count(0, 0));

	/*
	 * Now turn on dynamic sseu and verify we still get the
	 * default we just set.
	 */
	set_allow_dynamic_sseu(drm_fd, true);

	igt_assert_eq(device_slice_count, read_slice_count(0, 0));

	/* One last powergated config for the road... */
	sseu.slice_mask = pg_slice_mask;
	gem_context_set_param(drm_fd, &arg);

	igt_assert_eq(pg_slice_count, read_slice_count(0, 0));
}

igt_main
{
	int i, max_slices = 3, max_subslices = 3;
	drm_i915_getparam_t gp;

	igt_fixture {
		/* Use drm_open_driver to verify device existence */
		drm_fd = drm_open_driver(DRIVER_INTEL);
		igt_require_gem(drm_fd);

		devid = intel_get_drm_devid(drm_fd);

		/* Old kernel? */
		igt_require(kernel_has_per_context_sseu_support());

		gp.param = I915_PARAM_SLICE_MASK;
		gp.value = (int *) &device_slice_mask;
		do_ioctl(drm_fd, DRM_IOCTL_I915_GETPARAM, &gp);
		device_slice_count = __builtin_popcount(device_slice_mask);

		gp.param = I915_PARAM_SUBSLICE_MASK;
		gp.value = (int *) &device_subslice_mask;
		do_ioctl(drm_fd, DRM_IOCTL_I915_GETPARAM, &gp);
		device_subslice_count = __builtin_popcount(device_subslice_mask);

		igt_require(!get_allow_dynamic_sseu(drm_fd));
	}

	igt_subtest("sseu-invalid-engine") {
		igt_require(platform_has_per_context_sseu_support());
		test_sseu_invalid_engine();
	}

	igt_subtest("sseu-invalid-values") {
		igt_require(platform_has_per_context_sseu_support());
		test_sseu_invalid_values();
	}

	for (i = 1; i < max_slices; i++) {
		igt_subtest_f("sseu-%i-pg-slice-program-rcs", i) {
			igt_require(device_slice_count > i);
			igt_require(platform_has_per_context_sseu_support());

			set_allow_dynamic_sseu(drm_fd, true);
			test_sseu_slice_program_gt(i);
		}
	}

	for (i = 1; i < max_subslices; i++) {
		igt_subtest_f("sseu-%i-pg-subslice-program-rcs", i) {
			igt_require(device_subslice_count >= 2);
			igt_require(platform_has_per_context_sseu_support());

			/* Only available on some Atom platforms and Gen10+. */
			igt_require(IS_BROXTON(devid) || IS_GEMINILAKE(devid) ||
				    intel_gen(devid) >= 10);

			set_allow_dynamic_sseu(drm_fd, true);
			test_sseu_subslice_program_gt(i);
		}
	}

	igt_subtest("sseu-perf") {
		igt_require(platform_has_per_context_sseu_support());
		igt_require(device_slice_count > 1);
		set_allow_dynamic_sseu(drm_fd, true);
		test_sseu_perf();
	}

	igt_subtest("dynamic-sseu") {
		igt_require(platform_has_per_context_sseu_support());
		igt_require(device_slice_count > 1);
		test_dynamic_sseu();
	}

	igt_fixture {
		set_allow_dynamic_sseu(drm_fd, false);

		close(drm_fd);
	}
}
