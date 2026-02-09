#ifndef HW_DISPLAY_SANDPIPER_VPU_H
#define HW_DISPLAY_SANDPIPER_VPU_H

#include "hw/core/sysbus.h"
#include "ui/console.h"

#define TYPE_SANDPIPER_PALETTE "sandpiper-palette"
#define SANDPIPER_PALETTE(obj) \
    OBJECT_CHECK(SandpiperPaletteState, (obj), TYPE_SANDPIPER_PALETTE)

typedef struct SandpiperPaletteState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    uint32_t palette[256];
} SandpiperPaletteState;

#define TYPE_SANDPIPER_VPU "sandpiper-vpu"
#define SANDPIPER_VPU(obj) \
    OBJECT_CHECK(SandpiperVPUState, (obj), TYPE_SANDPIPER_VPU)

typedef struct SandpiperVPUState {
    SysBusDevice parent_obj;
    MemoryRegion iomem;
    QemuConsole *con;
    SandpiperPaletteState *palette;
    struct SandpiperVCPState *vcp;

    uint32_t vpage;
    uint32_t second_buffer;
    uint32_t vpage_b;
    uint32_t second_buffer_b;
    uint32_t mode_flags;
    uint8_t mixmode;
    bool layerb_enable;
    uint16_t keycolor;
    
    bool cmd_pending;
    uint8_t pending_cmd_opcode;

    QEMUTimer *vsync_timer;
    bool vblank_toggle;
    bool swap_pending;
    bool swap_pending_b;

    uint32_t fifo[1024];
    int fifo_head;
    int fifo_tail;
    int fifo_count;

    uint32_t shift_scanout;
    uint32_t shift_pixel;
} SandpiperVPUState;

#endif
