#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <gst/video/video.h>
#include <gst/video/video-format.h>

#define BEBO_SHMEM_NAME L"BEBO_SHARED_MEMORY_BUFFER"
#define BEBO_SHMEM_MUTEX L"BEBO_SHARED_MEMORY_BUFFER_MUTEX"
#define BEBO_SHMEM_DATA_SEM L"BEBO_SHARE_MEMORY_NEW_DATA_SEMAPHORE"
/*
 * Will use a ring buffer for frames, and will trigger semaphore when new items are in the buffer
 */
#ifdef __cplusplus
    extern "C" {
#endif


#pragma pack(push)
#pragma pack(16)

    struct frame_header {
        uint64_t dts;
        uint64_t pts;
        uint64_t duration;
        uint8_t discontinuity;
    };

    struct shmem {
        GstVideoInfo video_info;
        uint64_t shmem_size;
        uint64_t frame_offset;
        uint64_t frame_data_offset;
        uint64_t frame_size;
        uint64_t buffer_size;
        uint64_t count;
        uint64_t write_ptr; // TODO better name - not really a ptr more like frame count
        uint64_t read_ptr;
    };
#pragma pack(pop)
#ifdef __cplusplus
    }
#endif