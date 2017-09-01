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

#define _GNU_SOURCE
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

#include "intel_bufmgr.h"

#define MI_STORE_REGISTER_MEM (0x24 << 23)

#define MI_SET_PREDICATE      (0x1 << 23)
#define  MI_SET_PREDICATE_NOOP_NEVER (0)
#define  MI_SET_PREDICATE_1_SLICES   (5)
#define  MI_SET_PREDICATE_2_SLICES   (6)
#define  MI_SET_PREDICATE_3_SLICES   (7)

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

#ifndef I915_PARAM_SLICE_MASK
#define I915_PARAM_SLICE_MASK		(46)
#define I915_PARAM_SUBSLICE_MASK	(47)
#endif /* I915_PARAM_SLICE_MASK */

#ifndef I915_CONTEXT_PARAM_SSEU
#define I915_CONTEXT_PARAM_SSEU		0x6

struct drm_i915_gem_context_param_sseu {
	/*
	 * Engine to be configured or queried. Same value you would use with
	 * drm_i915_gem_execbuffer2.
	 */
	__u64 flags;

	union {
		struct {
			__u8 slice_mask;
			__u8 subslice_mask;
			__u8 min_eu_per_subslice;
			__u8 max_eu_per_subslice;
		} packed;
		__u64 value;
	};
};
#endif /* I915_CONTEXT_PARAM_SSEU */

static int drm_fd;
static int devid;
static uint64_t device_slice_mask = 0;
static uint64_t device_subslice_mask = 0;
static uint32_t device_slice_count = 0;
static uint32_t device_subslice_count = 0;

static uint64_t mask_minus(uint64_t mask)
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

static uint32_t
read_rpcs_reg(drm_intel_bufmgr *bufmgr,
	      drm_intel_context *context,
	      uint64_t engine,
	      uint32_t expected_slices)
{
	struct intel_batchbuffer *batch;
	drm_intel_bo *bo;
	uint32_t rpcs;
	int ret;

	batch = intel_batchbuffer_alloc(bufmgr, devid);
	igt_assert(batch);

	intel_batchbuffer_set_context(batch, context);

	bo = drm_intel_bo_alloc(bufmgr, "target bo", 4096, 4096);
	igt_assert(bo);

	/* Clear destination buffer. */
	ret = drm_intel_bo_map(bo, false /* write enable */);
	igt_assert_eq(ret, 0);
	memset(bo->virtual, 0, bo->size);
	drm_intel_bo_unmap(bo);

	/*
	 * Prior to Gen10 we can use the predicate to further verify
	 * that the hardware has been programmed correctly.
	 */
	if (expected_slices != 0 && intel_gen(devid) < 10) {
		BEGIN_BATCH(5, 1);
		OUT_BATCH(MI_SET_PREDICATE | (1 - 1) |
			  (MI_SET_PREDICATE_1_SLICES + expected_slices - 1));
	} else {
		BEGIN_BATCH(3, 1);
	}

	OUT_BATCH(MI_STORE_REGISTER_MEM | (4 - 2));
	OUT_BATCH(GEN8_R_PWR_CLK_STATE);
	OUT_RELOC(bo, I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION, 0);

	if (expected_slices != 0 && intel_gen(devid) < 10)
		OUT_BATCH(MI_SET_PREDICATE | (1 - 1) | MI_SET_PREDICATE_NOOP_NEVER);

	ADVANCE_BATCH();

	intel_batchbuffer_flush_on_ring(batch, engine);

	drm_intel_bo_wait_rendering(bo);

	ret = drm_intel_bo_map(bo, false /* write enable */);
	igt_assert_eq(ret, 0);

	rpcs = *((uint32_t *) bo->virtual);

	drm_intel_bo_unmap(bo);
	drm_intel_bo_unreference(bo);

	intel_batchbuffer_free(batch);

	return rpcs;
}


static uint32_t
read_slice_count(drm_intel_bufmgr *bufmgr,
		 drm_intel_context *context,
		 uint32_t expected_slice_count)
{
	return (read_rpcs_reg(bufmgr, context, I915_EXEC_RENDER,
			      expected_slice_count) & GEN8_RPCS_S_CNT_MASK)
		>> GEN8_RPCS_S_CNT_SHIFT;
}

static uint32_t
read_subslice_count(drm_intel_bufmgr *bufmgr,
		    drm_intel_context *context)
{
	return (read_rpcs_reg(bufmgr, context, I915_EXEC_RENDER, 0) & GEN8_RPCS_SS_CNT_MASK)
		>> GEN8_RPCS_SS_CNT_SHIFT;
}

static void
context_get_sseu_masks(drm_intel_context *context, uint64_t engine,
		       uint32_t *slice_mask, uint32_t *subslice_mask)
{
	struct drm_i915_gem_context_param arg;
	struct drm_i915_gem_context_param_sseu sseu;
	uint32_t context_id;
	int ret;

	memset(&sseu, 0, sizeof(sseu));
	sseu.flags = engine;

	ret = drm_intel_gem_context_get_id(context, &context_id);
	igt_assert_eq(ret, 0);

	memset(&arg, 0, sizeof(arg));
	arg.ctx_id = context_id;
	arg.param = I915_CONTEXT_PARAM_SSEU;
	arg.value = (uintptr_t) &sseu;

	do_ioctl(drm_fd, DRM_IOCTL_I915_GEM_CONTEXT_GETPARAM, &arg);

	if (slice_mask)
		*slice_mask = sseu.packed.slice_mask;
	if (subslice_mask)
		*subslice_mask = sseu.packed.subslice_mask;
}

static void
context_set_slice_mask(drm_intel_context *context, uint64_t engine,
		       uint32_t slice_mask)
{
	struct drm_i915_gem_context_param arg;
	struct drm_i915_gem_context_param_sseu sseu;
	uint32_t context_id;
	int ret;

	memset(&sseu, 0, sizeof(sseu));
	sseu.flags = engine;

	ret = drm_intel_gem_context_get_id(context, &context_id);
	igt_assert_eq(ret, 0);

	memset(&arg, 0, sizeof(arg));
	arg.ctx_id = context_id;
	arg.param = I915_CONTEXT_PARAM_SSEU;
	arg.value = (uintptr_t) &sseu;

	do_ioctl(drm_fd, DRM_IOCTL_I915_GEM_CONTEXT_GETPARAM, &arg);

	sseu.packed.slice_mask = slice_mask;

	do_ioctl(drm_fd, DRM_IOCTL_I915_GEM_CONTEXT_SETPARAM, &arg);
}

static void
context_set_subslice_mask(drm_intel_context *context, uint64_t engine,
			  uint32_t subslice_mask)
{
	struct drm_i915_gem_context_param arg;
	struct drm_i915_gem_context_param_sseu sseu;
	uint32_t context_id;
	int ret;

	memset(&sseu, 0, sizeof(sseu));
	sseu.flags = engine;

	ret = drm_intel_gem_context_get_id(context, &context_id);
	igt_assert_eq(ret, 0);

	memset(&arg, 0, sizeof(arg));
	arg.ctx_id = context_id;
	arg.param = I915_CONTEXT_PARAM_SSEU;
	arg.value = (uintptr_t) &sseu;

	do_ioctl(drm_fd, DRM_IOCTL_I915_GEM_CONTEXT_GETPARAM, &arg);

	sseu.packed.subslice_mask = subslice_mask;

	do_ioctl(drm_fd, DRM_IOCTL_I915_GEM_CONTEXT_SETPARAM, &arg);
}

static void rpcs_slice_program_gt(uint64_t engine)
{
	drm_intel_bufmgr *bufmgr;
	drm_intel_context *context1, *context2;
	uint32_t slice_mask;

	bufmgr = drm_intel_bufmgr_gem_init(drm_fd, 4096);
	igt_assert(bufmgr);

	context1 = drm_intel_gem_context_create(bufmgr);
	igt_assert(context1);

	context2 = drm_intel_gem_context_create(bufmgr);
	igt_assert(context2);

	context_set_slice_mask(context1, engine, mask_minus(device_slice_mask));
	context_set_slice_mask(context2, engine, device_slice_mask);

	context_get_sseu_masks(context1, engine, &slice_mask, NULL);
	igt_assert_eq(mask_minus(device_slice_mask), slice_mask);
	context_get_sseu_masks(context2, engine, &slice_mask, NULL);
	igt_assert_eq(device_slice_mask, slice_mask);

	/*
	 * Test false positives with predicates (only available on
	 * before Gen10).
	 */
	if (intel_gen(devid) < 10) {
		igt_assert_eq(0, read_slice_count(bufmgr, context1, device_slice_count));
	}

	igt_assert_eq(device_slice_count - 1,
		      read_slice_count(bufmgr, context1, device_slice_count - 1));
	igt_assert_eq(device_slice_count,
		      read_slice_count(bufmgr, context2, device_slice_count));

	context_set_slice_mask(context1, engine, device_slice_mask);
	context_set_slice_mask(context2, engine, mask_minus(device_slice_mask));

	context_get_sseu_masks(context1, engine, &slice_mask, NULL);
	igt_assert_eq(device_slice_mask, slice_mask);
	context_get_sseu_masks(context2, engine, &slice_mask, NULL);
	igt_assert_eq(mask_minus(device_slice_mask), slice_mask);

	igt_assert_eq(device_slice_count,
		      read_slice_count(bufmgr, context1, device_slice_count));
	igt_assert_eq(device_slice_count - 1,
		      read_slice_count(bufmgr, context2, device_slice_count - 1));

	if (device_slice_count >= 3) {
		context_set_slice_mask(context1, engine, device_slice_mask);
		context_set_slice_mask(context2, engine, mask_minus(mask_minus(device_slice_mask)));

		context_get_sseu_masks(context1, engine, &slice_mask, NULL);
		igt_assert_eq(device_slice_mask, slice_mask);
		context_get_sseu_masks(context2, engine, &slice_mask, NULL);
		igt_assert_eq(mask_minus(mask_minus(device_slice_mask)), slice_mask);

		igt_assert_eq(device_slice_count,
			      read_slice_count(bufmgr, context1, device_slice_count));
		igt_assert_eq(device_slice_count - 2,
			      read_slice_count(bufmgr, context2, device_slice_count - 2));
	}

	drm_intel_gem_context_destroy(context1);
	drm_intel_gem_context_destroy(context2);

	drm_intel_bufmgr_destroy(bufmgr);
}

static void rpcs_subslice_program_gt(uint64_t engine)
{
	drm_intel_bufmgr *bufmgr;
	drm_intel_context *context1, *context2;
	uint32_t subslice_mask;

	bufmgr = drm_intel_bufmgr_gem_init(drm_fd, 4096);
	igt_assert(bufmgr);

	context1 = drm_intel_gem_context_create(bufmgr);
	igt_assert(context1);

	context2 = drm_intel_gem_context_create(bufmgr);
	igt_assert(context2);

	context_set_subslice_mask(context1, engine, mask_minus(device_subslice_mask));
	context_set_subslice_mask(context2, engine, device_subslice_mask);

	context_get_sseu_masks(context1, engine, NULL, &subslice_mask);
	igt_assert_eq(mask_minus(device_subslice_mask), subslice_mask);
	context_get_sseu_masks(context2, engine, NULL, &subslice_mask);
	igt_assert_eq(device_subslice_mask, subslice_mask);

	igt_assert_eq(device_subslice_count - 1, read_subslice_count(bufmgr, context1));
	igt_assert_eq(device_subslice_count, read_subslice_count(bufmgr, context2));

	context_set_subslice_mask(context1, engine, device_subslice_mask);
	context_set_subslice_mask(context2, engine, mask_minus(device_subslice_mask));

	context_get_sseu_masks(context1, engine, NULL, &subslice_mask);
	igt_assert_eq(device_subslice_mask, subslice_mask);
	context_get_sseu_masks(context2, engine, NULL, &subslice_mask);
	igt_assert_eq(mask_minus(device_subslice_mask), subslice_mask);

	igt_assert_eq(device_subslice_count, read_subslice_count(bufmgr, context1));
	igt_assert_eq(device_subslice_count - 1, read_subslice_count(bufmgr, context2));

	if (device_subslice_count >= 3) {
		context_set_subslice_mask(context1, engine, device_subslice_mask);
		context_set_subslice_mask(context2, engine,
					  mask_minus(mask_minus(device_subslice_mask)));

		context_get_sseu_masks(context1, engine, &subslice_mask, NULL);
		igt_assert_eq(device_subslice_mask, subslice_mask);
		context_get_sseu_masks(context2, engine, &subslice_mask, NULL);
		igt_assert_eq(mask_minus(mask_minus(device_subslice_mask)), subslice_mask);

		igt_assert_eq(device_subslice_count, read_subslice_count(bufmgr, context2));
		igt_assert_eq(device_subslice_count - 2, read_subslice_count(bufmgr, context1));
	}

	drm_intel_gem_context_destroy(context1);
	drm_intel_gem_context_destroy(context2);

	drm_intel_bufmgr_destroy(bufmgr);
}

igt_main
{
	uint64_t engines[] = {
		I915_EXEC_RENDER,
		I915_EXEC_BSD,
		I915_EXEC_VEBOX,
	};
	int i;
	drm_i915_getparam_t gp;

	igt_fixture {
		/* Use drm_open_driver to verify device existence */
		drm_fd = drm_open_driver(DRIVER_INTEL);
		igt_require_gem(drm_fd);

		devid = intel_get_drm_devid(drm_fd);

		/* We can only program slice count from Gen8. */
		igt_skip_on(intel_gen(devid) < 8);
	}

	gp.param = I915_PARAM_SLICE_MASK;
	gp.value = (int *) &device_slice_mask;
	do_ioctl(drm_fd, DRM_IOCTL_I915_GETPARAM, &gp);
	device_slice_count = __builtin_popcount(device_slice_mask);

	gp.param = I915_PARAM_SUBSLICE_MASK;
	gp.value = (int *) &device_subslice_mask;
	do_ioctl(drm_fd, DRM_IOCTL_I915_GETPARAM, &gp);
	device_subslice_count = __builtin_popcount(device_subslice_mask);

	igt_subtest("rpcs-slice-program-rcs") {
		igt_require(device_slice_count >= 2);

		for (i = 0; i < ARRAY_SIZE(engines); i++)
			rpcs_slice_program_gt(engines[i]);
	}

	igt_subtest("rpcs-subslice-program-rcs") {
		igt_require(device_subslice_count >= 2);
		/* Only available on some Atom platforms and Gen10+. */
		igt_require(IS_BROXTON(devid) || IS_GEMINILAKE(devid) ||
			    intel_gen(devid) >= 10);

		for (i = 0; i < ARRAY_SIZE(engines); i++)
			rpcs_subslice_program_gt(engines[i]);
	}

	igt_fixture {
		close(drm_fd);
	}
}
