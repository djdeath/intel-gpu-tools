/*
 * Copyright (C) 2019 Intel Corporation
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
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <i915_drm.h>

#include "igt_core.h"
#include "intel_chipset.h"
#include "i915/perf.h"
#include "i915/perf_data.h"

#include "i915_perf_recorder_commands.h"

#define ALIGN(v, a) (((v) + (a)-1) & ~((a)-1))
#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof((arr)[0]))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))

struct circular_buffer {
	char   *data;
	size_t  allocated_size;
	size_t  size;
	size_t  beginpos;
	size_t  endpos;
};

struct chunk {
	char *data;
	size_t len;
};

static size_t
circular_available_size(const struct circular_buffer *buffer)
{
	assert(buffer->size <= buffer->allocated_size);
	return buffer->allocated_size - buffer->size;
}

static void
get_chunks(struct chunk *chunks, struct circular_buffer *buffer, bool write, size_t len)
{
	size_t offset = write ? buffer->endpos : buffer->beginpos;

	if (write)
		assert(circular_available_size(buffer) >= len);
	else
		assert(buffer->size >= len);

	chunks[0].data = &buffer->data[offset];

	if ((offset + len) > buffer->allocated_size) {
		chunks[0].len = buffer->allocated_size - offset;
		chunks[1].data = buffer->data;
		chunks[1].len = len - (buffer->allocated_size - offset);
	} else {
		chunks[0].len = len;
		chunks[1].data = NULL;
		chunks[1].len = 0;
	}
}

static ssize_t
circular_buffer_read(void *c, char *buf, size_t size)
{
	struct circular_buffer *buffer = c;
	struct chunk chunks[2];

	if (buffer->size < size)
		return -1;

	get_chunks(chunks, buffer, false, size);

	memcpy(buf, chunks[0].data, chunks[0].len);
	memcpy(buf + chunks[0].len, chunks[1].data, chunks[1].len);
	buffer->beginpos = (buffer->beginpos + size) % buffer->allocated_size;
	buffer->size -= size;

	return size;
}

static size_t
peek_item_size(struct circular_buffer *buffer)
{
	struct drm_i915_perf_record_header header;
	struct chunk chunks[2];

	if (!buffer->size)
		return 0;

	assert(buffer->size >= sizeof(header));

	get_chunks(chunks, buffer, false, sizeof(header));
	memcpy(&header, chunks[0].data, chunks[0].len);
	memcpy((char *) &header + chunks[0].len, chunks[1].data, chunks[1].len);

	return header.size;
}

static void
circular_shrink(struct circular_buffer *buffer, size_t size)
{
	size_t shrank = 0, item_size;

	assert(size <= buffer->allocated_size);

	while (shrank < size && buffer->size > (item_size = peek_item_size(buffer))) {
		assert(item_size > 0 && item_size <= buffer->allocated_size);

		buffer->beginpos = (buffer->beginpos + item_size) % buffer->allocated_size;
		buffer->size -= item_size;

		shrank += item_size;
	}
}

static ssize_t
circular_buffer_write(void *c, const char *buf, size_t _size)
{
	struct circular_buffer *buffer = c;
	size_t size = _size;

	while (size) {
		size_t avail = circular_available_size(buffer), item_size;
		struct chunk chunks[2];

		/* Make space in the buffer if there is too much data. */
		if (avail < size)
			circular_shrink(buffer, size - avail);

		item_size = MIN(circular_available_size(buffer), size);

		get_chunks(chunks, buffer, true, item_size);

		memcpy(chunks[0].data, buf, chunks[0].len);
		memcpy(chunks[1].data, buf + chunks[0].len, chunks[1].len);

		buf += item_size;
		size -= item_size;

		buffer->endpos = (buffer->endpos + item_size) % buffer->allocated_size;
		buffer->size += item_size;
	}

	return _size;
}

static int
circular_buffer_seek(void *c, off64_t *offset, int whence)
{
	return -1;
}

static int
circular_buffer_close(void *c)
{
	return 0;
}

cookie_io_functions_t circular_buffer_functions = {
	.read  = circular_buffer_read,
	.write = circular_buffer_write,
	.seek  = circular_buffer_seek,
	.close = circular_buffer_close,
};


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

static uint32_t
read_device_param(const char *stem, int id, const char *param)
{
	char *name;
	int ret = asprintf(&name, "/sys/class/drm/%s%u/device/%s", stem, id, param);
	uint64_t value;
	bool success;

	assert(ret != -1);

	success = read_file_uint64(name, &value);
	free(name);

	return success ? value : 0;
}

static int
find_intel_render_node(void)
{
	for (int i = 128; i < (128 + 16); i++) {
		if (read_device_param("renderD", i, "vendor") == 0x8086)
			return i;
	}

	return -1;
}

static int
open_render_node(uint32_t *devid)
{
	char *name;
	int ret;
	int fd;

	int render = find_intel_render_node();
	if (render < 0)
		return -1;

	ret = asprintf(&name, "/dev/dri/renderD%u", render);
	assert(ret != -1);

	*devid = read_device_param("renderD", render, "device");

	fd = open(name, O_RDWR);
	free(name);

	return fd;
}

static uint32_t
oa_exponent_for_period(uint64_t device_timestamp_frequency, double period)
{
	uint64_t period_ns = 1000 * 1000 * 1000 * period;
	uint64_t device_periods[32];

	for (uint32_t i = 0; i < ARRAY_SIZE(device_periods); i++)
		device_periods[i] = 1000000000ull * (1u << i) / device_timestamp_frequency;

	for (uint32_t i = 1; i < ARRAY_SIZE(device_periods); i++) {
		if (period_ns >= device_periods[i - 1] &&
		    period_ns < device_periods[i]) {
			if ((device_periods[i] - period_ns) >
			    (period_ns - device_periods[i - 1]))
				return i - 1;
			return i;
		}
	}

	return -1;
}

static int
perf_ioctl(int fd, unsigned long request, void *arg)
{
	int ret;

	do {
		ret = ioctl(fd, request, arg);
	} while (ret == -1 && (errno == EINTR || errno == EAGAIN));

	return ret;
}

static uint64_t
get_device_timestamp_frequency(const struct intel_device_info *devinfo, int drm_fd)
{
	drm_i915_getparam_t gp;
	int timestamp_frequency;

	gp.param = I915_PARAM_CS_TIMESTAMP_FREQUENCY;
	gp.value = &timestamp_frequency;
	if (perf_ioctl(drm_fd, DRM_IOCTL_I915_GETPARAM, &gp) == 0)
		return timestamp_frequency;

	if (devinfo->gen > 9) {
		fprintf(stderr, "Unable to query timestamp frequency from i915, please update kernel.\n");
		return 0;
	}

	fprintf(stderr, "Warning: unable to query timestamp frequency from i915, guessing values...\n");

	if (devinfo->gen <= 8)
		return 12500000;
	if (devinfo->is_broxton)
		return 19200000;
	return 12000000;
}

static int
perf_open(int drm_fd,
	  const struct intel_device_info *devinfo,
	  uint32_t oa_exponent,
	  const struct intel_perf_metric_set *metric_set)
{
	uint64_t properties[DRM_I915_PERF_PROP_MAX * 2];
	struct drm_i915_perf_open_param param;
	int p = 0, stream_fd;

	properties[p++] = DRM_I915_PERF_PROP_SAMPLE_OA;
	properties[p++] = true;

	properties[p++] = DRM_I915_PERF_PROP_OA_METRICS_SET;
	properties[p++] = metric_set->perf_oa_metrics_set;

	properties[p++] = DRM_I915_PERF_PROP_OA_FORMAT;
	properties[p++] = metric_set->perf_oa_format;

	properties[p++] = DRM_I915_PERF_PROP_OA_EXPONENT;
	properties[p++] = oa_exponent;

	memset(&param, 0, sizeof(param));
	param.flags = 0;
	param.flags |= I915_PERF_FLAG_FD_CLOEXEC | I915_PERF_FLAG_FD_NONBLOCK;
	param.properties_ptr = (uintptr_t)properties;
	param.num_properties = p / 2;

	stream_fd = perf_ioctl(drm_fd, DRM_IOCTL_I915_PERF_OPEN, &param);
	return stream_fd;
}

static bool quit = false;

static void
sigint_handler(int val)
{
	quit = true;
}

static bool
write_header(FILE *output,
	     uint32_t device_id,
	     uint64_t timestamp_frequency,
	     const struct intel_perf_metric_set *metric_set)
{
	struct intel_perf_record_device_info info = {
		.timestamp_frequency = timestamp_frequency,
		.device_id = device_id,
		.oa_format = metric_set->perf_oa_format,
	};
	struct drm_i915_perf_record_header header = {
		.type = INTEL_PERF_RECORD_TYPE_DEVICE_INFO,
		.size = sizeof(header) + sizeof(info),
	};

	snprintf(info.uuid, sizeof(info.uuid), "%s", metric_set->hw_config_guid);

	if (fwrite(&header, sizeof(header), 1, output) != 1)
		return false;

	if (fwrite(&info, sizeof(info), 1, output) != 1)
		return false;

	return true;
}

static bool
write_topology(FILE *output, int drm_fd)
{
	struct drm_i915_perf_record_header header = {
		.type = INTEL_PERF_RECORD_TYPE_DEVICE_TOPOLOGY,
	};
	struct drm_i915_query query = {};
	struct drm_i915_query_topology_info *topo_info;
	struct drm_i915_query_item item = {
		.query_id = DRM_I915_QUERY_TOPOLOGY_INFO,
	};
	int ret;

	query.num_items = 1;
	query.items_ptr = (uintptr_t) &item;

	/* Maybe not be available on older kernels. */
	ret = perf_ioctl(drm_fd, DRM_IOCTL_I915_QUERY, &query);
	if (ret < 0)
		return true;

	assert(item.length > 0);
	topo_info = malloc(item.length);
	item.data_ptr = (uintptr_t) topo_info;

	ret = perf_ioctl(drm_fd, DRM_IOCTL_I915_QUERY, &query);
	assert(ret == 0);

	header.size = sizeof(header) + item.length;
	if (fwrite(&header, sizeof(header), 1, output) != 1)
		return false;

	if (fwrite(topo_info, item.length, 1, output) != 1)
		return false;

	return true;
}

static bool
write_i915_perf_data(FILE *output, int perf_fd)
{
	ssize_t ret;
	char data[4096];

	while ((ret = read(perf_fd, data, sizeof(data))) > 0 ||
	       errno == EINTR) {
		if (fwrite(data, ret, 1, output) != 1)
			return false;
	}

	return true;
}

static uint64_t timespec_diff(struct timespec *begin,
			      struct timespec *end)
{
	return 1000000000ull * (end->tv_sec - begin->tv_sec) + end->tv_nsec - begin->tv_nsec;
}

static clock_t correlation_clock_id = CLOCK_MONOTONIC;

static bool
get_correlation_timestamps(struct intel_perf_record_timestamp_correlation *corr, int drm_fd)
{
	struct drm_i915_reg_read reg_read;
	struct {
		struct timespec cpu_ts_begin;
		struct timespec cpu_ts_end;
		uint64_t gpu_ts;
	} attempts[3];
	uint32_t best = 0;

#define RENDER_RING_TIMESTAMP 0x2358

        reg_read.offset = RENDER_RING_TIMESTAMP | I915_REG_READ_8B_WA;

	/* Gather 3 correlations. */
	for (uint32_t i = 0; i < ARRAY_SIZE(attempts); i++) {
		clock_gettime(correlation_clock_id, &attempts[i].cpu_ts_begin);
		if (perf_ioctl(drm_fd, DRM_IOCTL_I915_REG_READ, &reg_read) < 0)
			return false;
		clock_gettime(correlation_clock_id, &attempts[i].cpu_ts_end);

		attempts[i].gpu_ts = reg_read.val;
	}

	/* Now select the best. */
	for (uint32_t i = 1; i < ARRAY_SIZE(attempts); i++) {
		if (timespec_diff(&attempts[i].cpu_ts_begin,
				  &attempts[i].cpu_ts_end) <
		    timespec_diff(&attempts[best].cpu_ts_begin,
				  &attempts[best].cpu_ts_end))
			best = i;
	}

	corr->cpu_timestamp =
		(attempts[best].cpu_ts_begin.tv_sec * 1000000000ull +
		 attempts[best].cpu_ts_begin.tv_nsec) +
		timespec_diff(&attempts[best].cpu_ts_begin,
			      &attempts[best].cpu_ts_end) / 2;
	corr->gpu_timestamp = attempts[best].gpu_ts;

	return true;
}

static bool
write_saved_correlation_timestamps(FILE *output,
				   const struct intel_perf_record_timestamp_correlation *corr)
{
	struct drm_i915_perf_record_header header = {
		.type = INTEL_PERF_RECORD_TYPE_TIMESTAMP_CORRELATION,
		.size = sizeof(header) + sizeof(*corr),
	};

	if (fwrite(&header, sizeof(header), 1, output) != 1)
		return false;

	if (fwrite(corr, sizeof(*corr), 1, output) != 1)
		return false;

	return true;
}

static bool
write_correlation_timestamps(FILE *output, int drm_fd)
{
	struct intel_perf_record_timestamp_correlation corr;

	if (!get_correlation_timestamps(&corr, drm_fd))
		return false;

	return write_saved_correlation_timestamps(output, &corr);
}

static void
read_command_file(int command_fd, FILE *output_stream, struct circular_buffer *buffer,
		  int drm_fd, uint32_t devid, uint64_t timestamp_frequency,
		  struct intel_perf_metric_set *metric_set)
{
	struct recorder_command_base header;
	ssize_t ret = read(command_fd, &header, sizeof(header));

	if (ret < 0)
		return;

	switch (header.command) {
	case RECORDER_COMMAND_DUMP: {
		uint32_t len = header.size - sizeof(header), offset = 0;
		struct recorder_command_dump *dump = malloc(len);
		FILE *file;

		while (offset < len &&
		       ((ret = read(command_fd, (void *) dump + offset, len - offset)) > 0
			|| errno == EAGAIN)) {
			if (ret > 0)
				offset += ret;
		}

		fprintf(stdout, "Writing circular buffer to %s\n", dump->path);

		file = fopen((const char *) dump->path, "w+");
		if (file) {
			struct chunk chunks[2];

			fflush(output_stream);
			get_chunks(chunks, buffer, false, buffer->size);

			if (!write_header(file, devid, timestamp_frequency, metric_set) ||
			    !write_topology(file, drm_fd) ||
			    fwrite(chunks[0].data, chunks[0].len, 1, file) != 1 ||
			    (chunks[1].len > 0 &&
			     fwrite(chunks[1].data, chunks[1].len, 1, file) != 1) ||
			    !write_correlation_timestamps(file, drm_fd)) {
				fprintf(stderr, "Unable to write circular buffer data in file '%s'\n",
					dump->path);
			}
			fclose(file);
		} else
			fprintf(stderr, "Unable to write dump file '%s'\n", dump->path);

		free(dump);
		break;
	}
	case RECORDER_COMMAND_QUIT:
		quit = true;
		break;
	default:
		fprintf(stderr, "Unknown command 0x%x\n", header.command);
		break;
	}
}

static void
print_metric_sets(const struct intel_perf *perf)
{
	struct intel_perf_metric_set *metric_set;
	uint32_t longest_name = 0;

	igt_list_for_each(metric_set, &perf->metric_sets, link) {
		longest_name = MAX(longest_name, strlen(metric_set->symbol_name));
	}

	igt_list_for_each(metric_set, &perf->metric_sets, link) {
		fprintf(stdout, "%s:%*s%s\t\n",
			metric_set->symbol_name,
			(int) (longest_name - strlen(metric_set->symbol_name) + 1), " ",
			metric_set->name);
	}
}

static void
print_metric_set_counters(const struct intel_perf_metric_set *metric_set)
{
	uint32_t longest_name = 0;
	for (uint32_t i = 0; i < metric_set->n_counters; i++) {
		longest_name = MAX(longest_name, strlen(metric_set->counters[i].name));
	}

	fprintf(stdout, "Metric set %s:\n", metric_set->name);
	for (uint32_t i = 0; i < metric_set->n_counters; i++) {
		struct intel_perf_logical_counter *counter = &metric_set->counters[i];

		fprintf(stdout, "%s:%*s%s\n",
			counter->name,
			(int)(longest_name - strlen(counter->name) + 1), " ",
			counter->desc);
	}
}

static void
usage(const char *name)
{
	fprintf(stdout,
		"Usage: %s [options]\n"
		"\n"
		"     --help,               -h          Print this screen\n"
		"     --correlation-period, -c <value>  Time period of timestamp correlation in seconds\n"
		"                                       (default = 1.0)\n"
		"     --perf-period,        -p <value>  Time period of i915-perf reports in seconds\n"
		"                                       (default = 0.001)\n"
		"     --metric,             -m <value>  i915 metric to sample with\n"
		"     --counters,           -C          List counters for a given metric and exit\n"
		"     --size,               -s <value>  Size of circular buffer to use in kilobytes\n"
		"                                       If specified, a maximum amount of <value> data will\n"
		"                                       be recorded.\n"
		"     --command-fifo,       -f <path>   Path to a command fifo, implies circular buffer\n"
		"                                       (To use with i915-perf-control)\n"
		"     --output,             -o <path>   Output file (default = i915_perf.record)\n"
		"     --cpu-clock,          -k <path>   Cpu clock to use for correlations\n"
		"                                       Values: boot, mono, mono_raw (default = mono)\n",
		name);
}

int
main(int argc, char *argv[])
{
	const struct option long_options[] = {
		{"help",                       no_argument, 0, 'h'},
		{"correlation-period",   required_argument, 0, 'c'},
		{"perf-period",          required_argument, 0, 'p'},
		{"metric",               required_argument, 0, 'm'},
		{"counters",                   no_argument, 0, 'C'},
		{"output",               required_argument, 0, 'o'},
		{"size",                 required_argument, 0, 's'},
		{"command-fifo",         required_argument, 0, 'f'},
		{"cpu-clock",            required_argument, 0, 'k'},
		{0, 0, 0, 0}
	};
	const struct {
		clock_t id;
		const char *name;
	} clock_names[] = {
		{ CLOCK_BOOTTIME,      "boot" },
		{ CLOCK_MONOTONIC,     "mono" },
		{ CLOCK_MONOTONIC_RAW, "mono_raw" },
	};
	const struct intel_device_info *devinfo;
	double corr_period = 1.0, perf_period = 0.001;
	const char *metric_name = NULL, *output_file = "i915_perf.record", *command_fifo = I915_PERF_RECORD_FIFO_PATH;
	struct intel_perf *perf;
	struct intel_perf_metric_set *metric_set, *selected_metric_set = NULL;
	struct intel_perf_record_timestamp_correlation initial_correlation;
	struct circular_buffer circular_buffer;
	struct timespec now;
	uint64_t corr_period_ns, poll_time_ns, timestamp_frequency;
	uint32_t devid = 0, oa_exponent;
	uint32_t circular_size = 0;
	int drm_fd, perf_fd, command_fifo_fd = -1;
	int opt;
	bool list_counters = false;
	FILE *output = NULL, *output_stream;

	while ((opt = getopt_long(argc, argv, "hc:p:m:Co:s:f:k:", long_options, NULL)) != -1) {
		switch (opt) {
		case 'h':
			usage(argv[0]);
			return EXIT_SUCCESS;
		case 'c':
			corr_period = atof(optarg);
			break;
		case 'p':
			perf_period = atof(optarg);
			break;
		case 'm':
			metric_name = optarg;
			break;
		case 'C':
			list_counters = true;
			break;
		case 'o':
			output_file = optarg;
			break;
		case 's':
			circular_size = MAX(8, atoi(optarg)) * 1024;
			break;
		case 'f':
			command_fifo = optarg;
			circular_size = 8 * 1024 * 1024;
			break;
		case 'k': {
			bool found = false;
			for (uint32_t i = 0; i < ARRAY_SIZE(clock_names); i++) {
				if (!strcmp(clock_names[i].name, optarg)) {
					correlation_clock_id = clock_names[i].id;
					found = true;
					break;
				}
			}
			if (!found) {
				fprintf(stderr, "Unknown clock name '%s'\n", optarg);
				return EXIT_FAILURE;
			}
			break;
		}
		default:
			fprintf(stderr, "Internal error: "
				"unexpected getopt value: %d\n", opt);
			usage(argv[0]);
			return EXIT_FAILURE;
		}
	}

	drm_fd = open_render_node(&devid);

	devinfo = intel_get_device_info(devid);
	if (!devinfo) {
		fprintf(stderr, "No device info found.\n");
		return EXIT_FAILURE;
	}

	fprintf(stdout, "Device name=%s gen=%i gt=%i id=0x%x\n",
		devinfo->codename, devinfo->gen, devinfo->gt, devid);

	perf = intel_perf_for_devinfo(devinfo);
	if (!perf) {
		fprintf(stderr, "No perf data found.\n");
		return EXIT_FAILURE;
	}

	if (!metric_name) {
		print_metric_sets(perf);
		return EXIT_FAILURE;
	}

	igt_list_for_each(metric_set, &perf->metric_sets, link) {
		if (!strcasecmp(metric_set->symbol_name, metric_name)) {
			selected_metric_set = metric_set;
			break;
		}
	}

	if (!selected_metric_set) {
		fprintf(stderr, "Unknown metric set '%s'\n", metric_name);
		print_metric_sets(perf);
		return EXIT_FAILURE;
	}

	if (list_counters) {
		print_metric_set_counters(selected_metric_set);
		return EXIT_SUCCESS;
	}

	intel_perf_load_perf_configs(perf, drm_fd);

	timestamp_frequency = get_device_timestamp_frequency(devinfo, drm_fd);

	signal(SIGINT, sigint_handler);

	if (command_fifo) {
		if (mkfifo(command_fifo, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH) != 0) {
			fprintf(stderr, "Unable to create command fifo '%s': %s\n",
				command_fifo, strerror(errno));
			return EXIT_FAILURE;
		}

		command_fifo_fd = open(command_fifo, O_RDWR);
		if (command_fifo_fd < 0) {
			fprintf(stderr, "Unable to open command fifo '%s': %s\n",
				command_fifo, strerror(errno));
			return EXIT_FAILURE;
		}
	} else {
		output = fopen(output_file, "w+");
		if (!output) {
			fprintf(stderr, "Unable to open output file '%s'\n",
				output_file);
			return EXIT_FAILURE;
		}
	}

	if (circular_size) {
		circular_buffer.allocated_size = circular_size;
		circular_buffer.data = malloc(circular_size);
		if (!circular_buffer.data) {
			fprintf(stderr, "Unable to allocate circular buffer\n");
			return EXIT_FAILURE;
		}

		output_stream = fopencookie(&circular_buffer, "w+",
					    circular_buffer_functions);
		if (!output_stream) {
			fprintf(stderr, "Unable to create circular buffer\n");
			return EXIT_FAILURE;
		}

		if (!get_correlation_timestamps(&initial_correlation, drm_fd)) {
			fprintf(stderr, "Unable to correlation timestamps\n");
			return EXIT_FAILURE;
		}

		write_correlation_timestamps(output_stream, drm_fd);
	} else {
		if (!write_header(output, devid, timestamp_frequency, selected_metric_set) ||
		    !write_topology(output, drm_fd) ||
		    !write_correlation_timestamps(output, drm_fd)) {
			fprintf(stderr, "Unable to write header in file '%s'\n",
				output_file);
			return EXIT_FAILURE;
		}

		output_stream = output;
	}

	if (selected_metric_set->perf_oa_metrics_set == 0) {
		fprintf(stderr,
			"Unable to load performance configuration, consider running:\n"
			"   sysctl dev.i915.perf_stream_paranoid=0\n");
		return EXIT_FAILURE;
	}

	oa_exponent = oa_exponent_for_period(timestamp_frequency, perf_period);
	fprintf(stdout, "Opening perf stream with metric_id=%lu oa_exponent=%u\n",
		selected_metric_set->perf_oa_metrics_set, oa_exponent);

	perf_fd = perf_open(drm_fd, devinfo, oa_exponent, selected_metric_set);
	if (perf_fd < 0) {
		fprintf(stderr, "Unable to open i915 perf stream: %s\n",
			strerror(errno));
		return EXIT_FAILURE;
	}

	corr_period_ns = corr_period * 1000000000ul;
	poll_time_ns = corr_period_ns;

	while (!quit) {
		struct pollfd pollfd[2] = {
			{         perf_fd, POLLIN, 0 },
			{ command_fifo_fd, POLLIN, 0 },
		};
		uint64_t elapsed_ns;
		int ret;

		igt_gettime(&now);
		ret = poll(pollfd, command_fifo_fd != -1 ? 2 : 1, poll_time_ns / 1000000);
		if (ret < 0 && errno != EINTR) {
			fprintf(stderr, "Failed to poll i915-perf stream: %s\n",
				strerror(errno));
			break;
		}

		if (ret > 0) {
			if (pollfd[0].revents & POLLIN) {
				if (!write_i915_perf_data(output_stream, perf_fd)) {
					fprintf(stderr, "Failed to write i915-perf data: %s\n",
						strerror(errno));
					break;
				}
			}

			if (pollfd[1].revents & POLLIN) {
				read_command_file(command_fifo_fd, output_stream,
						  &circular_buffer,
						  drm_fd, devid, timestamp_frequency,
						  selected_metric_set);
			}
		}

		elapsed_ns = igt_nsec_elapsed(&now);
		if (elapsed_ns > poll_time_ns) {
			poll_time_ns = corr_period_ns;
			if (!write_correlation_timestamps(output_stream, drm_fd)) {
				fprintf(stderr,
					"Failed to write i915 timestamp correlation data: %s\n",
					strerror(errno));
				break;
			}
		} else {
			poll_time_ns -= elapsed_ns;
		}
	}

	fprintf(stdout, "Exiting...\n");

	if (!write_correlation_timestamps(output_stream, drm_fd)) {
		fprintf(stderr,
			"Failed to write final i915 timestamp correlation data: %s\n",
			strerror(errno));
	}

	fclose(output_stream);

	if (command_fifo)
		unlink(command_fifo);

	free(circular_buffer.data);

	close(drm_fd);

	return EXIT_SUCCESS;
}
