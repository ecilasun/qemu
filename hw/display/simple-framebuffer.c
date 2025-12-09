/*
 * Simple Framebuffer device for device-tree based systems
 *
 * This emulates the Linux "simple-framebuffer" device tree binding,
 * providing a basic framebuffer display from a memory region.
 *
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qapi/error.h"
#include "hw/sysbus.h"
#include "hw/qdev-properties.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "migration/vmstate.h"
#include "qom/object.h"
#include "exec/cpu-common.h"

#define TYPE_SIMPLE_FRAMEBUFFER "simple-framebuffer"
OBJECT_DECLARE_SIMPLE_TYPE(SimpleFramebufferState, SIMPLE_FRAMEBUFFER)

struct SimpleFramebufferState {
    SysBusDevice parent_obj;

    /* Properties from device tree */
    uint64_t fb_base;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    char *format;

    /* Internal state */
    QemuConsole *con;
    int invalidate;
    int bpp;
};

static void simple_fb_invalidate(void *opaque)
{
    SimpleFramebufferState *s = SIMPLE_FRAMEBUFFER(opaque);
    s->invalidate = 1;
}

static void simple_fb_update(void *opaque)
{
    SimpleFramebufferState *s = SIMPLE_FRAMEBUFFER(opaque);
    DisplaySurface *surface;
    uint8_t *dest;
    int dest_linesize;
    uint32_t y;
    hwaddr fb_size;
    void *fb_ptr;
    hwaddr len;

    surface = qemu_console_surface(s->con);
    if (!surface || !surface_data(surface)) {
        return;
    }

    dest = surface_data(surface);
    dest_linesize = surface_stride(surface);
    fb_size = (hwaddr)s->stride * s->height;

    /* Map framebuffer memory */
    len = fb_size;
    fb_ptr = cpu_physical_memory_map(s->fb_base, &len, false);
    if (!fb_ptr || len < fb_size) {
        if (fb_ptr) {
            cpu_physical_memory_unmap(fb_ptr, len, false, 0);
        }
        return;
    }

    /* Convert and copy framebuffer to display surface */
    for (y = 0; y < s->height; y++) {
        uint8_t *src_line = (uint8_t *)fb_ptr + y * s->stride;
        uint8_t *dest_line = dest + y * dest_linesize;
        uint32_t x;

        for (x = 0; x < s->width; x++) {
            uint8_t r, g, b;

            if (s->bpp == 16) {
                /* RGB565 */
                uint16_t pixel = lduw_le_p(src_line + x * 2);
                r = ((pixel >> 11) & 0x1f) << 3;
                g = ((pixel >> 5) & 0x3f) << 2;
                b = (pixel & 0x1f) << 3;
            } else if (s->bpp == 32) {
                /* ARGB8888 or XRGB8888 */
                uint32_t pixel = ldl_le_p(src_line + x * 4);
                r = (pixel >> 16) & 0xff;
                g = (pixel >> 8) & 0xff;
                b = pixel & 0xff;
            } else if (s->bpp == 24) {
                /* RGB888 */
                b = src_line[x * 3];
                g = src_line[x * 3 + 1];
                r = src_line[x * 3 + 2];
            } else {
                r = g = b = 0;
            }

            /* Write to 32-bit destination surface */
            ((uint32_t *)dest_line)[x] = rgb_to_pixel32(r, g, b);
        }
    }

    cpu_physical_memory_unmap(fb_ptr, len, false, 0);

    dpy_gfx_update_full(s->con);
    s->invalidate = 0;
}

static const GraphicHwOps simple_fb_ops = {
    .invalidate = simple_fb_invalidate,
    .gfx_update = simple_fb_update,
};

static void simple_fb_realize(DeviceState *dev, Error **errp)
{
    SimpleFramebufferState *s = SIMPLE_FRAMEBUFFER(dev);

    if (s->width == 0 || s->height == 0) {
        error_setg(errp, "simple-framebuffer: width and height must be set");
        return;
    }

    if (s->format == NULL) {
        s->format = g_strdup("r5g6b5");
    }

    /* Determine bpp from format and calculate stride if not provided */
    if (strcmp(s->format, "r5g6b5") == 0) {
        s->bpp = 16;
        if (s->stride == 0) {
            s->stride = s->width * 2;
        }
    } else if (strcmp(s->format, "a8r8g8b8") == 0 ||
               strcmp(s->format, "x8r8g8b8") == 0) {
        s->bpp = 32;
        if (s->stride == 0) {
            s->stride = s->width * 4;
        }
    } else if (strcmp(s->format, "r8g8b8") == 0) {
        s->bpp = 24;
        if (s->stride == 0) {
            s->stride = s->width * 3;
        }
    } else {
        /* Default to 16-bit */
        s->bpp = 16;
        if (s->stride == 0) {
            s->stride = s->width * 2;
        }
    }

    /* Create the graphics console */
    s->con = graphic_console_init(dev, 0, &simple_fb_ops, s);
    qemu_console_resize(s->con, s->width, s->height);

    s->invalidate = 1;
}

static const Property simple_fb_properties[] = {
    DEFINE_PROP_UINT64("base", SimpleFramebufferState, fb_base, 0),
    DEFINE_PROP_UINT32("width", SimpleFramebufferState, width, 640),
    DEFINE_PROP_UINT32("height", SimpleFramebufferState, height, 480),
    DEFINE_PROP_UINT32("stride", SimpleFramebufferState, stride, 0),
    DEFINE_PROP_STRING("format", SimpleFramebufferState, format),
};

static const VMStateDescription vmstate_simple_fb = {
    .name = "simple-framebuffer",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_INT32(invalidate, SimpleFramebufferState),
        VMSTATE_END_OF_LIST()
    }
};

static void simple_fb_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = simple_fb_realize;
    dc->vmsd = &vmstate_simple_fb;
    device_class_set_props(dc, simple_fb_properties);
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    dc->desc = "Simple Framebuffer (device-tree compatible)";
}

static const TypeInfo simple_fb_info = {
    .name          = TYPE_SIMPLE_FRAMEBUFFER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SimpleFramebufferState),
    .class_init    = simple_fb_class_init,
};

static void simple_fb_register_types(void)
{
    type_register_static(&simple_fb_info);
}

type_init(simple_fb_register_types)
