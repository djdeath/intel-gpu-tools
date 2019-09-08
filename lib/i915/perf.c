/*
 * Copyright (C) 2018 Intel Corporation
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

#include <stdlib.h>
#include <string.h>

#include "intel_chipset.h"
#include "perf.h"
#include "i915_perf_metrics.h"

struct intel_perf *
intel_perf_for_devinfo(const struct intel_device_info *devinfo)
{
	struct intel_perf_devinfo gputop_devinfo = {};

	if (devinfo->is_haswell)
		return intel_perf_get_metrics_hsw(&gputop_devinfo);
	if (devinfo->is_broadwell)
		return intel_perf_get_metrics_bdw(&gputop_devinfo);
	if (devinfo->is_cherryview)
		return intel_perf_get_metrics_chv(&gputop_devinfo);
	if (devinfo->is_skylake) {
		switch (devinfo->gt) {
		case 2:
			return intel_perf_get_metrics_sklgt2(&gputop_devinfo);
		case 3:
			return intel_perf_get_metrics_sklgt3(&gputop_devinfo);
		case 4:
			return intel_perf_get_metrics_sklgt4(&gputop_devinfo);
		default:
			return NULL;
		}
	}
	if (devinfo->is_broxton)
		return intel_perf_get_metrics_bxt(&gputop_devinfo);
	if (devinfo->is_kabylake) {
		switch (devinfo->gt) {
		case 2:
			return intel_perf_get_metrics_kblgt2(&gputop_devinfo);
		case 3:
			return intel_perf_get_metrics_kblgt3(&gputop_devinfo);
		default:
			return NULL;
		}
	}
	if (devinfo->is_geminilake)
		return intel_perf_get_metrics_glk(&gputop_devinfo);
	if (devinfo->is_coffeelake) {
		switch (devinfo->gt) {
		case 2:
			return intel_perf_get_metrics_cflgt2(&gputop_devinfo);
		case 3:
			return intel_perf_get_metrics_cflgt3(&gputop_devinfo);
		default:
			return NULL;
		}
	}
	if (devinfo->is_cannonlake)
		return intel_perf_get_metrics_cnl(&gputop_devinfo);
	if (devinfo->is_icelake)
		return intel_perf_get_metrics_icl(&gputop_devinfo);
	return NULL;
}

static struct intel_perf_logical_counter_group *
intel_perf_logical_counter_group_new(struct intel_perf *perf,
				     struct intel_perf_logical_counter_group *parent,
				     const char *name)
{
	struct intel_perf_logical_counter_group *group = calloc(1, sizeof(*group));

	group->name = strdup(name);

	igt_list_init(&group->counters);
	igt_list_init(&group->groups);

	if (parent)
		igt_list_add_tail(&group->link, &parent->groups);
	else
		igt_list_init(&group->link);

	return group;
}

struct intel_perf *
intel_perf_new(void)
{
	struct intel_perf *perf = calloc(1, sizeof(*perf));

	perf->root_group = intel_perf_logical_counter_group_new(perf, NULL, "");

	igt_list_init(&perf->metric_sets);

	return perf;
}

static void
intel_perf_logical_counter_group_free(struct intel_perf_logical_counter_group *group)
{
	struct intel_perf_logical_counter_group *child, *tmp;

	igt_list_for_each_safe(child, tmp, &group->groups, link) {
		igt_list_del(&child->link);
		intel_perf_logical_counter_group_free(child);
	}

	free(group->name);
	free(group);
}

static void
intel_perf_metric_set_free(struct intel_perf_metric_set *metric_set)
{
	free(metric_set->counters);
	free(metric_set);
}

void
intel_perf_free(struct intel_perf *perf)
{
	struct intel_perf_metric_set *metric_set, *tmp;

	intel_perf_logical_counter_group_free(perf->root_group);

	igt_list_for_each_safe(metric_set, tmp, &perf->metric_sets, link) {
		igt_list_del(&metric_set->link);
		intel_perf_metric_set_free(metric_set);
	}

	free(perf);
}

void
intel_perf_add_logical_counter(struct intel_perf *perf,
			       struct intel_perf_logical_counter *counter,
			       const char *group_path)
{
	const char *group_path_end = group_path + strlen(group_path);
	struct intel_perf_logical_counter_group *group = perf->root_group, *child_group = NULL;
	const char *name = group_path;

	while (name < group_path_end) {
		const char *name_end = strstr(name, "/");
		char group_name[128] = { 0, };
		struct intel_perf_logical_counter_group *iter_group;

		if (!name_end)
			name_end = group_path_end;

		memcpy(group_name, name, name_end - name);

		child_group = NULL;
		igt_list_for_each(iter_group, &group->groups, link) {
			if (!strcmp(iter_group->name, group_name)) {
				child_group = iter_group;
				break;
			}
		}

		if (!child_group)
			child_group = intel_perf_logical_counter_group_new(perf, group, group_name);

		name = name_end + 1;
		group = child_group;
	}

	igt_list_add_tail(&counter->link, &child_group->counters);
}

void
intel_perf_add_metric_set(struct intel_perf *perf,
			  struct intel_perf_metric_set *metric_set)
{
	igt_list_add_tail(&metric_set->link, &perf->metric_sets);
}
