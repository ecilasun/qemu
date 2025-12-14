#ifndef HW_AUDIO_SANDPIPER_APU_H
#define HW_AUDIO_SANDPIPER_APU_H

#include "hw/sysbus.h"
#include "qemu/audio.h"
#include "qom/object.h"

#define TYPE_SANDPIPER_APU "sandpiper-apu"
OBJECT_DECLARE_SIMPLE_TYPE(SandpiperAPUState, SANDPIPER_APU)

struct SandpiperAPUState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
    AudioBackend *card;
    SWVoiceOut *voice;

    uint32_t command_fifo[2];
    int command_fifo_level;

    /* Registers / State */
    uint32_t buffer_size_idx;
    uint32_t sample_rate_idx;
    uint32_t dma_address;
    bool channels_swapped;
    bool enabled;

    /* Playback State */
    uint32_t frame_status; /* 0 or 1 */
    uint32_t word_count;
    uint32_t read_cursor; /* In stereo samples */
    uint32_t buffer_samples; /* Total samples in current buffer size */
    
    /* Internal buffer to hold samples read from DMA */
    int16_t *sample_buffer;
    uint32_t sample_buffer_capacity;
};

#endif /* HW_AUDIO_SANDPIPER_APU_H */
