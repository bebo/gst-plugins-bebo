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
 * ATTENTION - MAKE SURE YOU INCREASE THE SHM_INTERFACE_VERSION WHEN YOU CHANGE THE SHM STRUCTS BELOW !
 */
#define SHM_INTERFACE_VERSION 1521597836

/*
 * Will use a ring buffer for frames, and will trigger semaphore when new items are in the buffer
 */
#ifdef __cplusplus
  extern "C" {
#endif

#pragma pack(push)
#pragma pack(16)

  struct frame {
    uint64_t nr;
    uint64_t dts;
    uint64_t pts;
    uint64_t latency;
    uint64_t duration;
    uint64_t size;
    HANDLE dxgi_handle;
    uint8_t discontinuity;
    uint8_t ref_cnt;
    void *_gst_buf_ref;
    //GstBuffer *_gst_buf_ref;
  };

  struct shmem {
    uint64_t version;
    GstVideoInfo video_info;
    uint64_t shmem_size;
    uint64_t frame_offset;
    uint64_t frame_size;
    uint64_t count;
    uint64_t write_ptr; // TODO better name - not really a ptr more like frame count
    uint64_t read_ptr;
  };
#pragma pack(pop)
#ifdef __cplusplus
    }
#endif
