#include "qemu/osdep.h"
#include "hw/display/sandpiper_vcp.h"
#include "hw/display/sandpiper_vpu.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "system/address-spaces.h"
#include "system/dma.h"

#define VCP_CMD_SETBUFFERSIZE 0x0
#define VCP_CMD_STARTDMA      0x1
#define VCP_CMD_EXEC          0x2

/* VCP Instructions */
#define VCP_NOOP            0x00
#define VCP_LOADIMM         0x01
#define VCP_PALWRITE        0x02
#define VCP_WAITSCANLINE    0x03
#define VCP_WAITPIXEL       0x04
#define VCP_MATHOP          0x05
#define VCP_JUMP            0x06
#define VCP_CMP             0x07
#define VCP_BRANCH          0x08
#define VCP_STORE           0x09
#define VCP_LOAD            0x0A
#define VCP_READSCANINFO	0x0B
#define VCP_UNUSED0			0x0C
#define VCP_LOGICOP         0x0D
#define VCP_UNUSED2			0x0E
#define VCP_UNUSED1			0x0F

#define DESTREG(inst)       ((inst >> 4) & 0xF)
#define SRCREG1(inst)       ((inst >> 8) & 0xF)
#define SRCREG2(inst)       ((inst >> 12) & 0xF)
#define IMMED24(inst)       ((inst >> 8) & 0xFFFFFF)
#define IMMED16(inst)       ((inst >> 16) & 0xFFFF)
#define IMMED8(inst)        ((inst >> 24) & 0xFF)

static void sandpiper_vcp_reset(DeviceState *dev)
{
    SandpiperVCPState *s = SANDPIPER_VCP(dev);
    memset(s->regs, 0, sizeof(s->regs));
    s->pc = 0;
    s->running = false;
    s->waiting = false;
    s->status = 0;
    s->cmd_state = VCP_STATE_IDLE;
    s->buffer_size = 0;
}

static uint64_t sandpiper_vcp_read(void *opaque, hwaddr offset, unsigned size)
{
    SandpiperVCPState *s = opaque;
    
    switch (offset) {
    case 0x00: /* Status / Command */
        /* 
         * Status bits from vcpdemo.c:
         * execstate = stat & 0xF
         * runstate = (stat >> 4) & 0xF
         * pc = (stat >> 8) & 0x1FFF
         * fifoempty = (stat >> 21) & 0x1
         * copystate = (stat >> 22) & 0x1
         * debugopcode = (stat >> 24) & 0xF
         */
        {
            uint32_t stat = 0;
            stat |= (s->running ? 1 : 0); // Simple run state
            stat |= (s->pc & 0x1FFF) << 8;
            return stat;
        }
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad read offset 0x%"HWADDR_PRIx"\n", __func__, offset);
        return 0;
    }
}

static void sandpiper_vcp_write(void *opaque, hwaddr offset, uint64_t val, unsigned size)
{
    SandpiperVCPState *s = opaque;

    switch (offset) {
    case 0x00: /* Command / Data */
        if (s->cmd_state == VCP_STATE_WAIT_BUFFER_SIZE) {
            s->buffer_size = val;
            s->cmd_state = VCP_STATE_IDLE;
            return;
        } else if (s->cmd_state == VCP_STATE_WAIT_DMA_ADDR) {
            uint32_t dma_addr = val;
            uint32_t len = s->buffer_size > 0 ? s->buffer_size : sizeof(s->program_mem);
            if (len > sizeof(s->program_mem)) {
                len = sizeof(s->program_mem);
            }
            dma_memory_read(&address_space_memory, dma_addr, s->program_mem, len, MEMTXATTRS_UNSPECIFIED);
            s->cmd_state = VCP_STATE_IDLE;
            return;
        }

        {
            uint32_t cmd = val & 0xF;
			uint32_t flags = (val >> 4) & 0xF;
            switch (cmd)
			{
				case VCP_CMD_SETBUFFERSIZE:
					s->cmd_state = VCP_STATE_WAIT_BUFFER_SIZE;
				break;
				case VCP_CMD_STARTDMA:
					s->cmd_state = VCP_STATE_WAIT_DMA_ADDR;
				break;
				case VCP_CMD_EXEC:
					s->running = flags & 0x1 ? true : false;
					s->waiting = false;
					s->pc = 0;
				break;
            }
        }
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad write offset 0x%"HWADDR_PRIx"\n", __func__, offset);
        break;
    }
}

static const MemoryRegionOps sandpiper_vcp_ops = {
    .read = sandpiper_vcp_read,
    .write = sandpiper_vcp_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

void sandpiper_vcp_set_vpu(SandpiperVCPState *s, SandpiperVPUState *vpu)
{
    s->vpu = vpu;
}

void sandpiper_vcp_reset_frame(SandpiperVCPState *s)
{
    /* Called at VSYNC? */
    /* Maybe reset waiting state if it was waiting for a line that passed? */
}

void sandpiper_vcp_run(SandpiperVCPState *s, uint32_t current_y, uint32_t current_x)
{
    if (!s->running) {
        return;
    }

    int instructions_executed = 0;
    const int MAX_INSTRUCTIONS = 1000; /* Prevent infinite loops */

	while (instructions_executed < MAX_INSTRUCTIONS)
	{
		if (s->waiting)
		{
			bool condition_met = false;
			if (s->wait_line != -1)
			{
				if (current_y >= s->wait_line)
				{
					condition_met = true;
				}
			}
			else if (s->wait_pixel != -1)
			{
				/* Wait for pixel on current line? Or absolute pixel? */
				/* Usually wait pixel is on the current line. */
				/* If we are on the same line and x >= wait_pixel */
				if (current_x >= s->wait_pixel)
				{
					condition_met = true;
				}
			}

			if (condition_met)
			{
				s->waiting = false;
				s->wait_line = -1;
				s->wait_pixel = -1;
				s->pc++; /* Advance past the wait instruction */
			}
			else
			{
				return; /* Still waiting */
			}
		}

		if (s->pc >= (VCP_MEM_SIZE / 4))
		{
			s->running = false;
			return;
		}

		uint32_t inst = s->program_mem[s->pc];
		uint32_t opcode = inst & 0xF;
		
		uint32_t dest = DESTREG(inst);
		uint32_t src1 = SRCREG1(inst);
		uint32_t src2 = SRCREG2(inst);
		uint32_t imm24 = IMMED24(inst);
		uint32_t imm16 = IMMED16(inst);
		uint32_t imm8 = IMMED8(inst);

		switch (opcode) {
			case VCP_NOOP:
				// Waste one clock
			break;
			case VCP_LOADIMM:
				s->regs[dest] = imm24;
			break;
			case VCP_PALWRITE:
			{
				if (s->vpu && s->vpu->palette)
				{
					uint32_t addr = s->regs[src1] & 0xFF;
					uint32_t val = s->regs[src2];
					s->vpu->palette->palette[addr] = val;
				}
			}
			break;
			case VCP_WAITSCANLINE:
				s->wait_line = s->regs[src1];
				s->wait_pixel = -1;
				s->waiting = true;
				return; /* Stop execution until condition met */
			case VCP_WAITPIXEL:
				s->wait_pixel = s->regs[src1];
				s->wait_line = -1;
				s->waiting = true;
				return; /* Stop execution until condition met */
			case VCP_MATHOP:
			{
				uint32_t v1 = s->regs[src1];
				uint32_t v2 = s->regs[src2];
				uint32_t res = 0;
				switch (imm8)
				{
					case 0x00: res = v1 + v2; break; /* ADD */
					case 0x01: res = v1 - v2; break; /* SUB */
					case 0x02: res = v1 + 1; break; /* INC */
					case 0x03: res = v1 - 1; break; /* DEC */
					default: res =0; break;
				}
				s->regs[dest] = res;
			}
            break;
			case VCP_JUMP:
				if (dest & 0x1)
				{
					/* Jump to immediate */
					s->pc = (s->pc * 4 + (int32_t)(int16_t)imm16) / 4;
				}
				else
				{
					/* Normal jump */
					s->pc = s->regs[src1] / 4; /* Address is in bytes, PC is in words */
				}
				continue; /* Don't increment PC at end of loop */
			case VCP_CMP:
			{
				uint32_t v1 = s->regs[src1];
				uint32_t v2 = s->regs[src2];
				bool res = false;
				switch (imm8)
				{
					case 0x01: res = (v1 <= v2); break; /* LE */
					case 0x02: res = (v1 < v2); break;  /* LT */
					case 0x04: res = (v1 == v2); break; /* EQ */
					case 0x09: res = (v1 > v2); break;  /* GT = LE | 0x8 */
					case 0x0A: res = (v1 >= v2); break; /* GE = LT | 0x8 */
					case 0x0C: res = (v1 != v2); break; /* NE = EQ | 0x8 */
				}
				s->cmpreg = res ? 1 : 0;
			}
			break;
			case VCP_BRANCH:
				if (s->cmpreg)
				{
					if (dest & 0x1)
					{
						/* Branch to immediate */
						s->pc = (s->pc + (signed int)imm16) / 4;
					}
					else
					{
						/* Normal branch */
						s->pc = s->regs[src1] / 4;
					}
					continue;
				}
			break;
			case VCP_STORE:
				{
					uint32_t addr = s->regs[src1] / 4;
					if (addr < (VCP_MEM_SIZE / 4))
					{
						s->program_mem[addr] = s->regs[src2];
					}
				}
				break;
			case VCP_LOAD:
				{
					uint32_t addr = s->regs[src1] / 4;
					if (addr < (VCP_MEM_SIZE / 4))
					{
						s->regs[dest] = s->program_mem[addr];
					}
				}
				break;
			case VCP_READSCANINFO:
				if (src1 & 0x1)
					s->regs[dest] = current_x;
				else
					s->regs[dest] = current_y;
			break;

			case VCP_UNUSED0:
				// 
			break;

			case VCP_LOGICOP:
			{
				uint32_t v1 = s->regs[src1];
				uint32_t v2 = s->regs[src2];
				uint32_t res = 0;
				switch (imm8)
				{
					case 0x00: res = v1 & v2; break; /* AND */
					case 0x01: res = v1 | v2; break; /* OR */
					case 0x02: res = v1 ^ v2; break; /* XOR */
					case 0x03: res = (int32_t)v1 >> (v2 & 0x1F); break; /* ASR */
					case 0x04: res = v1 >> (v2 & 0x1F); break; /* SHR */
					case 0x05: res = v1 << (v2 & 0x1F); break; /* SHL */
					case 0x06: res = ~v1; break; /* NEG/NOT */
					case 0x07: res = s->cmpreg; break; /* RCMP */
					case 0x08: res = 0; break; /* RCTL - TODO: Read VPU control register */
				}
				s->regs[dest] = res;
			}
			break;
			case VCP_UNUSED1:
				break;
			case VCP_UNUSED2:
				break;
		}

		s->pc++;
		instructions_executed++;
    }
}

static void sandpiper_vcp_init(Object *obj)
{
    SandpiperVCPState *s = SANDPIPER_VCP(obj);
    memory_region_init_io(&s->iomem, obj, &sandpiper_vcp_ops, s, "sandpiper-vcp", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->iomem);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
}

static void sandpiper_vcp_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->legacy_reset = sandpiper_vcp_reset;
}

static const TypeInfo sandpiper_vcp_info = {
    .name = TYPE_SANDPIPER_VCP,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SandpiperVCPState),
    .instance_init = sandpiper_vcp_init,
    .class_init = sandpiper_vcp_class_init,
};

static void sandpiper_vcp_register_types(void)
{
    type_register_static(&sandpiper_vcp_info);
}

type_init(sandpiper_vcp_register_types)
