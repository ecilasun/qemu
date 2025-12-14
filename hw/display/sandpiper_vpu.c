/*
 * Sandpiper Video Processing Unit (VPU) and Color Palette Module
 *
 * Copyright (c) 2025
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "hw/sysbus.h"
#include "hw/display/sandpiper_vpu.h"
#include "ui/console.h"
#include "ui/pixel_ops.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "system/address-spaces.h"

/* VPU Commands */
#define CMD_SETVPAGE        0x00
#define CMD_FINALIZE        0x01
#define CMD_VMODE           0x02
#define CMD_SHIFTCACHE      0x03
#define CMD_SHIFTSCANOUT    0x04
#define CMD_SHIFTPIXEL      0x05
#define CMD_SETSECONDBUFFER 0x06
#define CMD_SYNCSWAP        0x07
#define CMD_WCONTROLREG     0x08

/* VMODE Flags */
#define VMODE_SCAN_ENABLE   (1 << 0)
#define VMODE_WIDTH_640     (1 << 1)
#define VMODE_DEPTH_16BPP   (1 << 2)
#define VMODE_SCAN_DOUBLE   (1 << 3)

/* Palette Module */

static uint64_t sandpiper_palette_read(void *opaque, hwaddr offset,
                                       unsigned size)
{
    SandpiperPaletteState *s = SANDPIPER_PALETTE(opaque);
    unsigned int index = offset >> 2;

    if (index >= 256) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid palette read at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return 0;
    }
    return s->palette[index];
}

static void sandpiper_palette_write(void *opaque, hwaddr offset,
                                    uint64_t value, unsigned size)
{
    SandpiperPaletteState *s = SANDPIPER_PALETTE(opaque);
    unsigned int index = offset >> 2;

    if (index >= 256) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Invalid palette write at offset 0x%" HWADDR_PRIx "\n",
                      __func__, offset);
        return;
    }
    s->palette[index] = (uint32_t)value;
}

static const MemoryRegionOps sandpiper_palette_ops = {
    .read = sandpiper_palette_read,
    .write = sandpiper_palette_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void sandpiper_palette_init(Object *obj)
{
    SandpiperPaletteState *s = SANDPIPER_PALETTE(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &sandpiper_palette_ops, s,
                          "sandpiper-palette", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static const TypeInfo sandpiper_palette_info = {
    .name = TYPE_SANDPIPER_PALETTE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SandpiperPaletteState),
    .instance_init = sandpiper_palette_init,
};

/* VPU Module */

static void sandpiper_vpu_process_commands(SandpiperVPUState *s)
{
    while (s->fifo_count > 0) {
        if (s->swap_pending) {
            /* Stall processing until VSYNC */
            break;
        }

        uint32_t cmd_word = s->fifo[s->fifo_tail];
        s->fifo_tail = (s->fifo_tail + 1) % 1024;
        s->fifo_count--;

        /* If we are expecting a parameter, consume it */
        if (s->cmd_pending) {
            switch (s->pending_cmd_opcode) {
            case CMD_SETVPAGE:
                s->vpage = cmd_word;
                break;
            case CMD_VMODE:
                s->mode_flags = cmd_word;
                break;
            case CMD_SETSECONDBUFFER:
                s->second_buffer = cmd_word;
                break;
            case CMD_SHIFTCACHE:
            case CMD_SHIFTSCANOUT:
            case CMD_SHIFTPIXEL:
                /* Ignored for now */
                break;
            default:
                break;
            }
            s->cmd_pending = false;
            continue;
        }

        /* New Command */
        uint8_t opcode = cmd_word & 0xFF;
        
        switch (opcode) {
        case CMD_SETVPAGE:
        case CMD_VMODE:
        case CMD_SETSECONDBUFFER:
        case CMD_SHIFTCACHE:
        case CMD_SHIFTSCANOUT:
        case CMD_SHIFTPIXEL:
            s->pending_cmd_opcode = opcode;
            s->cmd_pending = true;
            break;
        case CMD_SYNCSWAP:
            /* Swap buffers at next VBLANK */
            s->swap_pending = true;
            break;
        case CMD_WCONTROLREG:
            /* Handle control reg */
            break;
        case CMD_FINALIZE:
        default:
            break;
        }
    }
}

static void sandpiper_vpu_vsync_timer_cb(void *opaque)
{
    SandpiperVPUState *s = SANDPIPER_VPU(opaque);

    s->vblank_toggle = !s->vblank_toggle;

    if (s->swap_pending) {
        uint32_t tmp = s->vpage;
        s->vpage = s->second_buffer;
        s->second_buffer = tmp;
        s->swap_pending = false;
    }

    /* Resume processing commands (e.g. barrier) */
    sandpiper_vpu_process_commands(s);

    timer_mod(s->vsync_timer, qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + NANOSECONDS_PER_SECOND / 60);
}

static void sandpiper_vpu_update_display(void *opaque)
{
    SandpiperVPUState *s = SANDPIPER_VPU(opaque);
    DisplaySurface *surface = qemu_console_surface(s->con);
    uint32_t *dest;
    uint8_t *src;
    uint16_t *src16;
    int width, height;
    int bpp;
    int stride;
    int y, x;
    hwaddr vpage_phys;
    MemoryRegion *mr;
    void *vram_ptr;
    hwaddr len;
    int src_stride;

    if (!(s->mode_flags & VMODE_SCAN_ENABLE)) {
        return;
    }

    width = (s->mode_flags & VMODE_WIDTH_640) ? 640 : 320;
    height = (s->mode_flags & VMODE_SCAN_DOUBLE) ? 240 : 480;
    bpp = (s->mode_flags & VMODE_DEPTH_16BPP) ? 16 : 8;

    if (width == 320 && bpp == 8) {
        src_stride = 384;
    } else {
        src_stride = width * (bpp / 8);
    }

    /* Check if surface needs resizing */
    if (surface_width(surface) != width || surface_height(surface) != height) {
        qemu_console_resize(s->con, width, height);
        surface = qemu_console_surface(s->con);
    }

    vpage_phys = s->vpage;
    if (vpage_phys == 0) {
        return;
    }

    /* Get pointer to guest RAM */
    /* Note: This is a simplification. Real hardware does DMA. 
       We assume the framebuffer is in the main system RAM. */
    len = src_stride * height;
    mr = address_space_translate(&address_space_memory, vpage_phys, &vpage_phys, &len, false, MEMTXATTRS_UNSPECIFIED);
    if (!mr || !memory_region_is_ram(mr)) {
        return;
    }
    vram_ptr = qemu_map_ram_ptr(mr->ram_block, vpage_phys);
    if (!vram_ptr) {
        return;
    }

    dest = (uint32_t *)surface_data(surface);
    stride = surface_stride(surface) / 4;

    if (bpp == 8) {
        src = (uint8_t *)vram_ptr;
        uint32_t *palette = s->palette ? s->palette->palette : NULL;

        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                uint8_t idx = src[y * src_stride + x];
                if (palette) {
                    /* Palette format 0x00RRGGBB -> QEMU 0x00RRGGBB */
                    dest[y * stride + x] = palette[idx];
                } else {
                    dest[y * stride + x] = idx * 0x010101; /* Grayscale fallback */
                }
            }
        }
    } else {
        /* 16bpp (RGB565) */
        src16 = (uint16_t *)vram_ptr;
        for (y = 0; y < height; y++) {
            for (x = 0; x < width; x++) {
                uint16_t pixel = src16[y * width + x];
                /* Convert RGB565 to RGB888 */
                uint8_t r = (pixel >> 11) & 0x1F;
                uint8_t g = (pixel >> 5) & 0x3F;
                uint8_t b = pixel & 0x1F;
                r = (r << 3) | (r >> 2);
                g = (g << 2) | (g >> 4);
                b = (b << 3) | (b >> 2);
                dest[y * stride + x] = (r << 16) | (g << 8) | b;
            }
        }
    }

    dpy_gfx_update(s->con, 0, 0, width, height);
}

static void sandpiper_vpu_invalidate_display(void *opaque)
{
    /* Force full redraw */
}

static const GraphicHwOps sandpiper_vpu_gfx_ops = {
    .invalidate = sandpiper_vpu_invalidate_display,
    .gfx_update = sandpiper_vpu_update_display,
};

static uint64_t sandpiper_vpu_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    SandpiperVPUState *s = SANDPIPER_VPU(opaque);

    if (offset == 0) {
        /* Status Register */
        /* Bit 0: blanktoggle (toggle every vsync) */
        /* Bit 11: !FIFO_EMPTY - 1 if FIFO is NOT empty */
        uint32_t status = s->vblank_toggle;
        if (s->fifo_count > 0) {
            status |= (1 << 11);
        }
        return status; 
    }
    return 0;
}

static void sandpiper_vpu_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    SandpiperVPUState *s = SANDPIPER_VPU(opaque);
    uint32_t cmd_word = (uint32_t)value;

    if (s->fifo_count < 1024) {
        s->fifo[s->fifo_head] = cmd_word;
        s->fifo_head = (s->fifo_head + 1) % 1024;
        s->fifo_count++;
    } else {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: FIFO Overflow\n", __func__);
    }

    sandpiper_vpu_process_commands(s);
}

static const MemoryRegionOps sandpiper_vpu_ops = {
    .read = sandpiper_vpu_read,
    .write = sandpiper_vpu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static void sandpiper_vpu_init(Object *obj)
{
    SandpiperVPUState *s = SANDPIPER_VPU(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);

    memory_region_init_io(&s->iomem, obj, &sandpiper_vpu_ops, s,
                          "sandpiper-vpu", 0x1000);
    sysbus_init_mmio(sbd, &s->iomem);
}

static void sandpiper_vpu_realize(DeviceState *dev, Error **errp)
{
    SandpiperVPUState *s = SANDPIPER_VPU(dev);

    s->con = graphic_console_init(dev, 0, &sandpiper_vpu_gfx_ops, s);
    s->vsync_timer = timer_new_ns(QEMU_CLOCK_REALTIME, sandpiper_vpu_vsync_timer_cb, s);
}

static const Property sandpiper_vpu_properties[] = {
    DEFINE_PROP_LINK("palette", SandpiperVPUState, palette,
                     TYPE_SANDPIPER_PALETTE, SandpiperPaletteState *),
};

static void sandpiper_vpu_reset(DeviceState *dev)
{
    SandpiperVPUState *s = SANDPIPER_VPU(dev);

    /* Default to simple-framebuffer configuration */
    s->vpage = 0x18000000;
    s->mode_flags = VMODE_SCAN_ENABLE | VMODE_WIDTH_640 | VMODE_DEPTH_16BPP;
    s->second_buffer = 0;
    s->cmd_pending = false;
    s->pending_cmd_opcode = 0;
    s->vblank_toggle = false;
    s->swap_pending = false;
    s->fifo_head = 0;
    s->fifo_tail = 0;
    s->fifo_count = 0;
    timer_mod(s->vsync_timer, qemu_clock_get_ns(QEMU_CLOCK_REALTIME));
}

static void sandpiper_vpu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sandpiper_vpu_realize;
    device_class_set_legacy_reset(dc, sandpiper_vpu_reset);
    device_class_set_props(dc, sandpiper_vpu_properties);
}

static const TypeInfo sandpiper_vpu_info = {
    .name = TYPE_SANDPIPER_VPU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SandpiperVPUState),
    .instance_init = sandpiper_vpu_init,
    .class_init = sandpiper_vpu_class_init,
};

/* VCP Module (Stub) */

static uint64_t sandpiper_vcp_read(void *opaque, hwaddr offset,
                                   unsigned size)
{
    return 0;
}

static void sandpiper_vcp_write(void *opaque, hwaddr offset,
                                uint64_t value, unsigned size)
{
    /* Stub */
}

static const MemoryRegionOps sandpiper_vcp_ops = {
    .read = sandpiper_vcp_read,
    .write = sandpiper_vcp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void sandpiper_vcp_init(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    MemoryRegion *iomem = g_new(MemoryRegion, 1);

    memory_region_init_io(iomem, obj, &sandpiper_vcp_ops, NULL,
                          "sandpiper-vcp", 0x1000);
    sysbus_init_mmio(sbd, iomem);
}

static const TypeInfo sandpiper_vcp_info = {
    .name = TYPE_SANDPIPER_VCP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_init = sandpiper_vcp_init,
};

static void sandpiper_vpu_register_types(void)
{
    type_register_static(&sandpiper_palette_info);
    type_register_static(&sandpiper_vpu_info);
    type_register_static(&sandpiper_vcp_info);
}

type_init(sandpiper_vpu_register_types)
