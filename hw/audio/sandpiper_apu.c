#include "qemu/osdep.h"
#include "hw/audio/sandpiper_apu.h"
#include "hw/qdev-properties.h"
#include "qemu/module.h"
#include "qemu/log.h"
#include "qemu/audio.h"
#include "exec/cpu-common.h"

#define APUCMD_BUFFERSIZE   0x0
#define APUCMD_START        0x1
#define APUCMD_NOOP         0x2
#define APUCMD_SWAPCHANNELS 0x3
#define APUCMD_SETRATE      0x4

static const uint32_t buffer_sizes[] = { 32, 64, 128, 256, 512, 1024 };
static const int sample_rates[] = { 44100, 22050, 11025, 0 };

static void sandpiper_apu_audio_callback(void *opaque, int free);

static void sandpiper_apu_reset(DeviceState *dev)
{
    SandpiperAPUState *s = SANDPIPER_APU(dev);

    s->command_fifo_level = 0;
    s->buffer_size_idx = 0;
    s->sample_rate_idx = 3; /* Halt */
    s->dma_address = 0;
    s->channels_swapped = false;
    s->enabled = false;
    s->frame_status = 0;
    s->word_count = buffer_sizes[0] - 1;
    s->read_cursor = 0;
    s->buffer_samples = buffer_sizes[0];

    memset(s->sample_buffer, 0, s->sample_buffer_capacity * sizeof(int16_t));
}

static void sandpiper_apu_process_command(SandpiperAPUState *s)
{
    uint32_t cmd = s->command_fifo[0];
    uint32_t arg = s->command_fifo[1];

    switch (cmd) {
    case APUCMD_BUFFERSIZE:
        if (arg < ARRAY_SIZE(buffer_sizes)) {
            s->buffer_size_idx = arg;
            s->buffer_samples = buffer_sizes[arg];
            s->word_count = s->buffer_samples - 1;
            /* Reset read cursor? Hardware seems to do it on reset or maybe not? */
        }
        break;
    case APUCMD_START:
        s->dma_address = arg;
        /* 
         * In hardware, this triggers DMA to fill the back buffer.
         * For simulation, we can read the data now into the back buffer area.
         * We use a double buffer scheme in sample_buffer:
         * 0..buffer_samples-1 : Front Buffer (Playing)
         * buffer_samples..2*buffer_samples-1 : Back Buffer (Filling)
         * 
         * Actually, to simplify, we can just read into the "back" part of our buffer.
         * The frame_status indicates which half is playing.
         * If frame_status == 0, playing from 0, filling 1.
         * If frame_status == 1, playing from 1, filling 0.
         */
        {
            int back_buffer_idx = (s->frame_status ^ 1);
            int offset = back_buffer_idx * s->buffer_samples * 2; /* 2 channels */
            
            /* Read from guest memory */
            cpu_physical_memory_read(s->dma_address, 
                                     &s->sample_buffer[offset], 
                                     s->buffer_samples * 2 * sizeof(int16_t));
        }
        break;
    case APUCMD_SWAPCHANNELS:
        s->channels_swapped = (arg != 0);
        break;
    case APUCMD_SETRATE:
        if (arg < ARRAY_SIZE(sample_rates)) {
            s->sample_rate_idx = arg;
            if (s->sample_rate_idx == 3) {
                s->enabled = false;
                AUD_set_active_out(s->voice, 0);
            } else {
                s->enabled = true;
                struct audsettings as;
                as.freq = sample_rates[s->sample_rate_idx];
                as.nchannels = 2;
                as.fmt = AUDIO_FORMAT_S16;
                as.endianness = 0; /* Little endian */
                s->voice = AUD_open_out(s->card, s->voice, "sandpiper-apu", s, sandpiper_apu_audio_callback, &as);
                AUD_set_active_out(s->voice, 1);
            }
        }
        break;
    default:
        break;
    }
}

static uint64_t sandpiper_apu_read(void *opaque, hwaddr offset, unsigned size)
{
    SandpiperAPUState *s = opaque;
    
    /* 
     * Status register:
     * Bit 0: Frame status
     * Bits 1-10: Word count (buffer size - 1)
     */
    uint32_t status = (s->word_count << 1) | (s->frame_status & 1);
    return status;
}

static void sandpiper_apu_write(void *opaque, hwaddr offset, uint64_t value, unsigned size)
{
    SandpiperAPUState *s = opaque;

    if (value == APUCMD_NOOP) {
        /* NOOP is immediate, doesn't take an argument */
        /* It is used for sync, ensuring previous commands are done. 
           Since we process immediately, we don't need to do anything. */
        return;
    }

    s->command_fifo[s->command_fifo_level++] = value;

    if (s->command_fifo_level == 2) {
        sandpiper_apu_process_command(s);
        s->command_fifo_level = 0;
    }
}

static const MemoryRegionOps sandpiper_apu_ops = {
    .read = sandpiper_apu_read,
    .write = sandpiper_apu_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void sandpiper_apu_audio_callback(void *opaque, int free)
{
    SandpiperAPUState *s = opaque;

    if (!s->enabled || !s->voice) {
        return;
    }

    int samples_to_play = free / (2 * sizeof(int16_t)); /* Stereo S16 */
    
    while (samples_to_play > 0) {
        int samples_available = s->buffer_samples - s->read_cursor;
        int chunk = MIN(samples_to_play, samples_available);

        if (chunk > 0) {
            int buffer_idx = s->frame_status;
            int offset = (buffer_idx * s->buffer_samples + s->read_cursor) * 2;
            
            /* Handle channel swapping if needed */
            /* For now, assuming no swap or doing it in place? 
               Hardware does it on output. We can just copy. */
            
            /* We need to copy to a temporary buffer if we want to swap, 
               or just write directly if not. */
            
            /* AUD_write takes bytes */
            int bytes = chunk * 2 * sizeof(int16_t);
            int16_t *src = &s->sample_buffer[offset];
            
            if (s->channels_swapped) {
                /* Slow path for swapped channels */
                int16_t *temp = g_new(int16_t, chunk * 2);
                for (int i = 0; i < chunk; i++) {
                    temp[i*2] = src[i*2+1];
                    temp[i*2+1] = src[i*2];
                }
                AUD_write(s->voice, temp, bytes);
                g_free(temp);
            } else {
                AUD_write(s->voice, src, bytes);
            }

            s->read_cursor += chunk;
            samples_to_play -= chunk;
        }

        if (s->read_cursor >= s->buffer_samples) {
            /* Buffer finished, swap */
            s->frame_status ^= 1;
            s->read_cursor = 0;
            
            /* 
             * Note: In hardware, if the next buffer isn't ready (DMA didn't happen),
             * it might play old data or silence. 
             * Here we just play whatever is in the buffer.
             * The guest is responsible for calling APUCMD_START to fill the back buffer
             * before we swap to it.
             */
        }
    }
}

static void sandpiper_apu_realize(DeviceState *dev, Error **errp)
{
    SandpiperAPUState *s = SANDPIPER_APU(dev);

    if (!AUD_backend_check(&s->card, errp)) {
        return;
    }

    memory_region_init_io(&s->iomem, OBJECT(dev), &sandpiper_apu_ops, s, "sandpiper-apu", 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->iomem);

    /* Allocate max buffer size: 2 buffers * 1024 samples * 2 channels */
    s->sample_buffer_capacity = 2 * 1024 * 2;
    s->sample_buffer = g_new0(int16_t, s->sample_buffer_capacity);
}

static void sandpiper_apu_unrealize(DeviceState *dev)
{
    SandpiperAPUState *s = SANDPIPER_APU(dev);
    g_free(s->sample_buffer);
}

static const Property sandpiper_apu_properties[] = {
    DEFINE_AUDIO_PROPERTIES(SandpiperAPUState, card),
};

static void sandpiper_apu_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sandpiper_apu_realize;
    dc->unrealize = sandpiper_apu_unrealize;
    dc->legacy_reset = sandpiper_apu_reset;
    dc->desc = "Sandpiper Audio Processing Unit";
    device_class_set_props(dc, sandpiper_apu_properties);
}

static const TypeInfo sandpiper_apu_info = {
    .name = TYPE_SANDPIPER_APU,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(SandpiperAPUState),
    .class_init = sandpiper_apu_class_init,
};

static void sandpiper_apu_register_types(void)
{
    type_register_static(&sandpiper_apu_info);
}

type_init(sandpiper_apu_register_types)
