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

#ifndef IGT_SYNCOBJ_H
#define IGT_SYNCOBJ_H

#include <stdint.h>
#include <stdbool.h>
#include <drm.h>

uint32_t syncobj_create(int fd, uint32_t flags);
void syncobj_destroy(int fd, uint32_t handle);
int __syncobj_handle_to_fd(int fd, struct drm_syncobj_handle *args);
int __syncobj_fd_to_handle(int fd, struct drm_syncobj_handle *args);
int __syncobj_fd_to_handle2(int fd, struct drm_syncobj_handle2 *args);
int syncobj_handle_to_fd(int fd, uint32_t handle, uint32_t flags);
uint32_t syncobj_fd_to_handle(int fd, int syncobj_fd, uint32_t flags);
void syncobj_import_sync_file(int fd, uint32_t handle, int sync_file);
void syncobj_import_sync_file2(int fd, uint32_t handle, uint64_t point, int sync_file);
int __syncobj_wait(int fd, struct drm_syncobj_wait *args);
int syncobj_wait_err(int fd, uint32_t *handles, uint32_t count,
		     uint64_t abs_timeout_nsec, uint32_t flags);
bool syncobj_wait(int fd, uint32_t *handles, uint32_t count,
		  uint64_t abs_timeout_nsec, uint32_t flags,
		  uint32_t *first_signaled);
bool syncobj_wait2(int fd, struct drm_syncobj_item *items, uint32_t count,
		   uint64_t abs_timeout_nsec, uint32_t flags,
		   uint32_t *first_signaled);
void syncobj_reset(int fd, uint32_t *handles, uint32_t count);
void syncobj_reset2(int fd, struct drm_syncobj_item *items, uint32_t count);
void syncobj_signal(int fd, uint32_t *handles, uint32_t count);
void syncobj_signal2(int fd, struct drm_syncobj_item *items, uint32_t count);
void syncobj_signal_point(int fd, uint32_t handle, uint64_t point);
int __syncobj_read_timeline(int fd, struct drm_syncobj_item *items, int count);
uint64_t syncobj_read_timeline(int fd, uint32_t handle);

#endif /* IGT_SYNCOBJ_H */
