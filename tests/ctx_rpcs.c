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


#ifndef I915_CONTEXT_PARAM_SSEU

#define I915_CONTEXT_PARAM_SSEU		0x6

union drm_i915_gem_context_param_sseu {
	struct {
		__u8 slice_mask;
		__u8 subslice_mask;
		__u8 min_eu_per_subslice;
		__u8 max_eu_per_subslice;
	} packed;
	__u64 value;
};

#endif /* I915_CONTEXT_PARAM_SSEU */

static int drm_fd;
static int devid;

static uint32_t
read_rpcs_reg(drm_intel_bufmgr *bufmgr,
	      drm_intel_context *context)
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

	BEGIN_BATCH(3, 1);
	OUT_BATCH(MI_STORE_REGISTER_MEM | (4 - 2));
	OUT_BATCH(GEN8_R_PWR_CLK_STATE);
	OUT_RELOC(bo, I915_GEM_DOMAIN_INSTRUCTION, I915_GEM_DOMAIN_INSTRUCTION, 0);
	ADVANCE_BATCH();

	intel_batchbuffer_flush_on_ring(batch, I915_EXEC_RENDER);

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
		 drm_intel_context *context)
{
	return (read_rpcs_reg(bufmgr, context) & GEN8_RPCS_S_CNT_MASK)
		>> GEN8_RPCS_S_CNT_SHIFT;
}

static void
context_set_slice_count(drm_intel_context *context, uint32_t mask)
{
	struct drm_i915_gem_context_param arg;
	union drm_i915_gem_context_param_sseu *sseu;
	uint32_t context_id;
	int ret;

	ret = drm_intel_gem_context_get_id(context, &context_id);
	igt_assert_eq(ret, 0);

	memset(&arg, 0, sizeof(arg));
	arg.ctx_id = context_id;
	arg.param = I915_CONTEXT_PARAM_SSEU;
	sseu = (union drm_i915_gem_context_param_sseu *) &arg.value;

	do_ioctl(drm_fd, DRM_IOCTL_I915_GEM_CONTEXT_GETPARAM, &arg);

	sseu->packed.slice_mask = mask;

	do_ioctl(drm_fd, DRM_IOCTL_I915_GEM_CONTEXT_SETPARAM, &arg);
}

static void rpcs_program_gt(uint32_t gt)
{
	drm_intel_bufmgr *bufmgr;
	drm_intel_context *context1, *context2;


	bufmgr = drm_intel_bufmgr_gem_init(drm_fd, 4096);
	igt_assert(bufmgr);

	context1 = drm_intel_gem_context_create(bufmgr);
	igt_assert(context1);

	context2 = drm_intel_gem_context_create(bufmgr);
	igt_assert(context2);

	switch (gt) {
	case 3:
		context_set_slice_count(context1, 0x1);
		context_set_slice_count(context2, 0x3);

		igt_assert_eq(1, read_slice_count(bufmgr, context1));
		igt_assert_eq(2, read_slice_count(bufmgr, context2));

		context_set_slice_count(context1, 0x3);
		context_set_slice_count(context2, 0x1);

		igt_assert_eq(2, read_slice_count(bufmgr, context1));
		igt_assert_eq(1, read_slice_count(bufmgr, context2));
		break;

	case 4:
		context_set_slice_count(context1, 0x1);
		context_set_slice_count(context2, 0x7);

		igt_assert_eq(3, read_slice_count(bufmgr, context2));
		igt_assert_eq(1, read_slice_count(bufmgr, context1));

		context_set_slice_count(context1, 0x5);

		igt_assert_eq(2, read_slice_count(bufmgr, context1));
		break;

	default:
		igt_assert(0);
		break;
	}

	drm_intel_gem_context_destroy(context1);
	drm_intel_gem_context_destroy(context2);

	drm_intel_bufmgr_destroy(bufmgr);
}

igt_main
{
	igt_fixture {
		/* Use drm_open_driver to verify device existence */
		drm_fd = drm_open_driver(DRIVER_INTEL);
		igt_require_gem(drm_fd);

		devid = intel_get_drm_devid(drm_fd);

		/* Skip on Atom platforms (can you even slice?). */
		igt_skip_on(IS_CHERRYVIEW(devid) || IS_BROXTON(devid) || IS_GEMINILAKE(devid));

		/* We can only program slice count from Gen8. */
		igt_skip_on(intel_gen(devid) < 8);
	}

	fprintf(stderr, "devid=%x\n", devid);
	fprintf(stderr, "gt=%u\n", intel_gt(devid));

	igt_subtest("rpcs-program-gt3") {
		/* intel_gt() returns (GT - 1). */
		igt_skip_on(intel_gt(devid) < 2);
		rpcs_program_gt(3);
	}

	igt_subtest("rpcs-program-gt4") {
		igt_skip_on(intel_gt(devid) < 3);
		rpcs_program_gt(4);
	}

	igt_fixture {
		close(drm_fd);
	}
}
