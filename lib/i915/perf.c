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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>

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

static bool
read_file_uint64(const char *file, uint64_t *value)
{
	char buf[32];
	int fd, n;

	fd = open(file, 0);
	if (fd < 0)
		return false;
	n = read(fd, buf, sizeof (buf) - 1);
	close(fd);
	if (n < 0)
		return false;

	buf[n] = '\0';
	*value = strtoull(buf, 0, 0);

	return true;
}

static int
get_card_for_fd(int fd)
{
	struct stat sb;
	int mjr, mnr;
	char buffer[128];
	DIR *drm_dir;
	struct dirent *entry;
	int retval = -1;

	if (fstat(fd, &sb))
		return -1;

	mjr = major(sb.st_rdev);
	mnr = minor(sb.st_rdev);

	snprintf(buffer, sizeof(buffer), "/sys/dev/char/%d:%d/device/drm", mjr, mnr);

	drm_dir = opendir(buffer);
	assert(drm_dir != NULL);

	while ((entry = readdir(drm_dir))) {
		if (entry->d_type == DT_DIR && strncmp(entry->d_name, "card", 4) == 0) {
			retval = strtoull(entry->d_name + 4, NULL, 10);
			break;
		}
	}

	closedir(drm_dir);

	return retval;
}

static void
load_metric_set_config(struct intel_perf_metric_set *metric_set, int drm_fd)
{
	struct drm_i915_perf_oa_config config;
	uint64_t config_id = 0;

	memset(&config, 0, sizeof(config));

	memcpy(config.uuid, metric_set->hw_config_guid, sizeof(config.uuid));

	config.n_mux_regs = metric_set->n_mux_regs;
	config.mux_regs_ptr = (uintptr_t) metric_set->mux_regs;

	config.n_boolean_regs = metric_set->n_b_counter_regs;
	config.boolean_regs_ptr = (uintptr_t) metric_set->b_counter_regs;

	config.n_flex_regs = metric_set->n_flex_regs;
	config.flex_regs_ptr = (uintptr_t) metric_set->flex_regs;

	while (ioctl(drm_fd, DRM_IOCTL_I915_PERF_ADD_CONFIG, &config) < 0 &&
	       (errno == EAGAIN || errno == EINTR));

	metric_set->perf_oa_metrics_set = config_id;
}

void
intel_perf_load_perf_configs(struct intel_perf *perf, int drm_fd)
{
	int drm_card = get_card_for_fd(drm_fd);
	struct dirent *entry;
	char metrics_path[128];
	DIR *metrics_dir;
	struct intel_perf_metric_set *metric_set;

	snprintf(metrics_path, sizeof(metrics_path),
		 "/sys/class/drm/card%d/metrics", drm_card);
	metrics_dir = opendir(metrics_path);
	if (!metrics_dir)
		return;

	while ((entry = readdir(metrics_dir))) {
		char *metric_id_path;
		uint64_t metric_id;

		if (entry->d_type != DT_DIR)
			continue;

		asprintf(&metric_id_path, "%s/%s/id",
			 metrics_path, entry->d_name);

		if (!read_file_uint64(metric_id_path, &metric_id)) {
			free(metric_id_path);
			continue;
		}

		free(metric_id_path);

		igt_list_for_each(metric_set, &perf->metric_sets, link) {
			if (!strcmp(metric_set->hw_config_guid, entry->d_name)) {
				metric_set->perf_oa_metrics_set = metric_id;
				break;
			}
		}
	}

	closedir(metrics_dir);

	igt_list_for_each(metric_set, &perf->metric_sets, link) {
		if (metric_set->perf_oa_metrics_set)
			continue;

		load_metric_set_config(metric_set, drm_fd);
	}
}
