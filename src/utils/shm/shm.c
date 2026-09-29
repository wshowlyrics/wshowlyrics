// SPDX-License-Identifier: GPL-3.0-or-later AND MIT AND CC0-1.0
/*
 * Shared-memory buffer pool, inherited from wshowkeys (shm.c).
 *
 * randname(), create_shm_file() and allocate_shm_file() come from the
 * "Allocating a shared memory pool" boilerplate in The Wayland Protocol book
 * by Drew DeVault (src/surfaces/shared-memory.md,
 * https://git.sr.ht/~sircmpwn/wayland-book), which the book releases into the
 * public domain / under CC0.
 *
 * buffer_release(), create_buffer(), destroy_buffer() and get_next_buffer()
 * are derived from sway's client/pool-buffer.c
 * (https://github.com/swaywm/sway), distributed under the following license:
 *
 * Copyright (c) 2016-2017 Drew DeVault
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Modifications for wshowlyrics are licensed under GPL-3.0-or-later.
 */
#include <assert.h>
#include <cairo/cairo.h>
#include <errno.h>
#include <fcntl.h>
#include <pango/pangocairo.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include "shm.h"
#include <wayland-client.h>

static void randname(char *buf) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A'+(r&15)+(r&16)*2;
        r >>= 5;
    }
}

int create_shm_file(void) {
    int retries = 100;
    do {
        char name[] = "/wl_shm-XXXXXX";
        randname(name + strlen(name) - 6);

        --retries;
        // CLOEXEC is guaranteed to be set by shm_open
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);

    return -1;
}

int allocate_shm_file(size_t size) {
    const int fd = create_shm_file();
    if (fd < 0) {
        return -1;
    }

    int ret;
    do {
        ret = ftruncate(fd, size);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void buffer_release(void *data, struct wl_buffer *const wl_buffer) {
    (void)wl_buffer;  // Required by Wayland callback signature
    struct pool_buffer *buffer = data;
    buffer->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release
};

static struct pool_buffer *create_buffer(struct wl_shm *shm,
        struct pool_buffer *buf, int32_t width, int32_t height,
        uint32_t format) {
    const size_t stride = (size_t)width * 4;
    const size_t size = stride * (size_t)height;

    const int fd = allocate_shm_file(size);
    assert(fd != -1);
    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    buf->buffer = wl_shm_pool_create_buffer(pool, 0,
            width, height, stride, format);
    wl_shm_pool_destroy(pool);
    close(fd);

    buf->size = size;
    buf->width = width;
    buf->height = height;
    buf->data = data;
    buf->surface = cairo_image_surface_create_for_data(data,
            CAIRO_FORMAT_ARGB32, width, height, stride);
    buf->cairo = cairo_create(buf->surface);
    buf->pango = pango_cairo_create_context(buf->cairo);

    wl_buffer_add_listener(buf->buffer, &buffer_listener, buf);
    return buf;
}

void destroy_buffer(struct pool_buffer *buffer) {
    if (buffer->buffer) {
        wl_buffer_destroy(buffer->buffer);
    }
    if (buffer->cairo) {
        cairo_destroy(buffer->cairo);
    }
    if (buffer->surface) {
        cairo_surface_destroy(buffer->surface);
    }
    if (buffer->pango) {
        g_object_unref(buffer->pango);
    }
    if (buffer->data) {
        munmap(buffer->data, buffer->size);
    }
    memset(buffer, 0, sizeof(struct pool_buffer));
}

struct pool_buffer *get_next_buffer(struct wl_shm *shm,
        struct pool_buffer pool[2], const uint32_t width, const uint32_t height) {
    // A zero dimension yields a zero-sized SHM pool, which is a fatal Wayland
    // protocol error. Callers must have a sized surface before requesting a buffer.
    if (width == 0 || height == 0) {
        return NULL;
    }

    struct pool_buffer *buffer = NULL;

    for (size_t i = 0; i < 2; ++i) {
        if (pool[i].busy) {
            continue;
        }
        buffer = &pool[i];
    }

    if (!buffer) {
        return NULL;
    }

    if (buffer->width != width || buffer->height != height) {
        destroy_buffer(buffer);
    }

    if (!buffer->buffer &&
        !create_buffer(shm, buffer, width, height, WL_SHM_FORMAT_ARGB8888)) {
        return NULL;
    }
    buffer->busy = true;
    return buffer;
}
