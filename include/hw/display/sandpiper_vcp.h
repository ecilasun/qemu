#ifndef HW_DISPLAY_SANDPIPER_VCP_H
#define HW_DISPLAY_SANDPIPER_VCP_H

#include "qemu/osdep.h"
#include "hw/core/sysbus.h"
#include "qom/object.h"

#define TYPE_SANDPIPER_VCP "sandpiper-vcp"
OBJECT_DECLARE_SIMPLE_TYPE(SandpiperVCPState, SANDPIPER_VCP)

#define VCP_REGS 16
#define VCP_MEM_SIZE 4096 // 4KB program memory? SDK says "128 << size", max size?

typedef struct SandpiperVCPState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    qemu_irq irq;

    /* Internal State */
	uint32_t cmpreg;
    uint32_t regs[VCP_REGS];
    uint32_t pc;
    uint32_t program_mem[VCP_MEM_SIZE / 4];
    
    /* Control/Status */
    uint32_t status;
    uint32_t control;
    
    /* Command Processing State */
    enum {
        VCP_STATE_IDLE,
        VCP_STATE_WAIT_BUFFER_SIZE,
        VCP_STATE_WAIT_DMA_ADDR
    } cmd_state;
    uint32_t buffer_size;

    /* Execution State */
    bool running;
    bool waiting;
    uint32_t wait_line;
    uint32_t wait_pixel;

    /* Link to VPU */
    struct SandpiperVPUState *vpu;

} SandpiperVCPState;

/* Public Interface for VPU */
void sandpiper_vcp_set_vpu(SandpiperVCPState *s, struct SandpiperVPUState *vpu);
void sandpiper_vcp_run(SandpiperVCPState *s, uint32_t current_y, uint32_t current_x);
void sandpiper_vcp_reset_frame(SandpiperVCPState *s);

#endif /* HW_DISPLAY_SANDPIPER_VCP_H */
