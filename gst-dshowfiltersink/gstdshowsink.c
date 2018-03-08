/* GStreamer
 * Copyright (C) <2009> Collabora Ltd
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk
 * Copyright (C) <2009> Nokia Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
/**
 * SECTION:element-shmsink
 * @title: shmsink
 *
 * Send data over shared memory to the matching source.
 *
 * ## Example launch lines
 * |[
 * gst-launch-1.0 -v videotestsrc ! "video/x-raw, format=YUY2, color-matrix=sdtv, \
 * chroma-site=mpeg2, width=(int)320, height=(int)240, framerate=(fraction)30/1" \
 * ! shmsink socket-path=/tmp/blah shm-size=2000000
 * ]| Send video to shm buffers.
 *
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdshowsink.h"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>
#include "../shared/bebo_shmem.h"
#include <windows.h>


/* signals */
enum
{
  SIGNAL_CLIENT_CONNECTED,
  SIGNAL_CLIENT_DISCONNECTED,
  LAST_SIGNAL
};

/* properties */
enum
{
  PROP_0,
  PROP_WAIT_FOR_CONNECTION,
  PROP_BUFFER_TIME
};


G_DEFINE_TYPE(GstShmSinkAllocator, gst_shm_sink_allocator, GST_TYPE_ALLOCATOR);

#define DEFAULT_WAIT_FOR_CONNECTION (FALSE)



GST_DEBUG_CATEGORY_STATIC (shmsink_debug);
#define GST_CAT_DEFAULT shmsink_debug

//TODO: define caps
static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);


#define gst_shm_sink_parent_class parent_class
G_DEFINE_TYPE (GstShmSink, gst_shm_sink, GST_TYPE_BASE_SINK);

static void gst_shm_sink_finalize (GObject * object);
static void gst_shm_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_shm_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_shm_sink_start (GstBaseSink * bsink);
static gboolean gst_shm_sink_stop (GstBaseSink * bsink);
static GstFlowReturn gst_shm_sink_render (GstBaseSink * bsink, GstBuffer * buf);

static gboolean gst_shm_sink_event (GstBaseSink * bsink, GstEvent * event);
static gboolean gst_shm_sink_unlock (GstBaseSink * bsink);
static gboolean gst_shm_sink_unlock_stop (GstBaseSink * bsink);
static gboolean gst_shm_sink_propose_allocation (GstBaseSink * sink,
    GstQuery * query);



static guint signals[LAST_SIGNAL] = { 0 };



/********************
 * CUSTOM ALLOCATOR *
 ********************/

static void
gst_shm_sink_allocator_dispose (GObject * object)
{
  GstShmSinkAllocator *self = GST_SHM_SINK_ALLOCATOR (object);

  if (self->sink)
    gst_object_unref (self->sink);
  self->sink = NULL;

  G_OBJECT_CLASS (gst_shm_sink_allocator_parent_class)->dispose (object);
}

static void
gst_shm_sink_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstDShowSinkMemory *mymem = (GstDShowSinkMemory *) mem;

  if (mymem->block) {
    GST_OBJECT_LOCK (mymem->sink);

    //sp_writer_free_block (mymem->block);
    GST_OBJECT_UNLOCK (mymem->sink);
    gst_object_unref (mymem->sink);
  }
  gst_object_unref (mem->allocator);

  g_slice_free (GstDShowSinkMemory, mymem);
}

static gpointer
gst_shm_sink_allocator_mem_map (GstMemory * mem, gsize maxsize,
    GstMapFlags flags)
{
  GstDShowSinkMemory *mymem = (GstDShowSinkMemory *) mem;

  return mymem->data;
}

static void
gst_shm_sink_allocator_mem_unmap (GstMemory * mem)
{
}

static GstMemory *
gst_shm_sink_allocator_mem_share (GstMemory * mem, gssize offset, gssize size)
{
  GstDShowSinkMemory *mymem = (GstDShowSinkMemory *) mem;
  GstDShowSinkMemory *mysub;
  GstMemory *parent;

  /* find the real parent */
  if ((parent = mem->parent) == NULL)
    parent = mem;

  if (size == -1)
    size = mem->size - offset;

  mysub = g_slice_new0 (GstDShowSinkMemory);
  /* the shared memory is always readonly */
  gst_memory_init (GST_MEMORY_CAST (mysub), GST_MINI_OBJECT_FLAGS (parent) |
      GST_MINI_OBJECT_FLAG_LOCK_READONLY, gst_object_ref (mem->allocator),
      parent, mem->maxsize, mem->align, mem->offset + offset, size);
  mysub->data = mymem->data;

  return (GstMemory *) mysub;
}

static gboolean
gst_shm_sink_allocator_mem_is_span (GstMemory * mem1, GstMemory * mem2,
    gsize * offset)
{

	DebugBreak();
  GstDShowSinkMemory *mymem1 = (GstDShowSinkMemory *) mem1;
  GstDShowSinkMemory *mymem2 = (GstDShowSinkMemory *) mem2;

  if (offset) {
    GstMemory *parent;

    parent = mem1->parent;

    *offset = mem1->offset - parent->offset;
  }

  /* and memory is contiguous */
  return mymem1->data + mem1->offset + mem1->size ==
      mymem2->data + mem2->offset;
}

static void
gst_shm_sink_allocator_init (GstShmSinkAllocator * self)
{

	GST_DEBUG_OBJECT(self, "-------> gst_shm_sink_allocator_init");
  GstAllocator *allocator = GST_ALLOCATOR (self);

  allocator->mem_map = gst_shm_sink_allocator_mem_map;
  allocator->mem_unmap = gst_shm_sink_allocator_mem_unmap;
  allocator->mem_share = gst_shm_sink_allocator_mem_share;
  allocator->mem_is_span = gst_shm_sink_allocator_mem_is_span;
}


static GstMemory *
gst_dshowvideo_sink_allocator_alloc_locked(GstShmSinkAllocator * self, gsize size,
	GstAllocationParams * params)
{


	GST_LOG_OBJECT(self,
		"gst_dshowvideo_sink_allocator_alloc_locked");

    GstMemory *memory = NULL;

	gsize maxsize = size + params->prefix + params->padding;
	gsize align = params->align;

	/* ensure configured alignment */
	align |= gst_memory_alignment;
	/* allocate more to compensate for alignment */
	maxsize += align;

	GstDShowSinkMemory *mymem;
	gsize aoffset, padding;



	mymem = g_slice_new0(GstDShowSinkMemory);
	memory = GST_MEMORY_CAST(mymem);
	mymem->sink = gst_object_ref(self->sink);


	uint64_t i = self->sink->shmem->write_ptr % self->sink->shmem->count;
	self->sink->shmem->write_ptr++;
	uint64_t frame_offset = self->sink->shmem->frame_offset + i * self->sink->shmem->frame_size;
	uint64_t data_offset = self->sink->shmem->frame_data_offset;

	struct frame_header *frame = ((struct frame_header*) (((unsigned char*)self->sink->shmem) + frame_offset));
	void *data = ((char*)frame) + data_offset;
	mymem->data = data;

	/* do alignment */
	if ((aoffset = ((guintptr)mymem->data & align))) {
		aoffset = (align + 1) - aoffset;
		mymem->data += aoffset;
		maxsize -= aoffset;
	}

	if (params->prefix && (params->flags & GST_MEMORY_FLAG_ZERO_PREFIXED))
		memset(mymem->data, 0, params->prefix);

	padding = maxsize - (params->prefix + size);
	if (padding && (params->flags & GST_MEMORY_FLAG_ZERO_PADDED))
		memset(mymem->data + params->prefix + size, 0, padding);

	gst_memory_init(memory, params->flags, g_object_ref(self), NULL,
		maxsize, align, params->prefix, size);

	return memory;
}

static GstMemory *
gst_dshowvideo_sink_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{

  GstShmSinkAllocator *self = GST_SHM_SINK_ALLOCATOR (allocator);
  GstMemory *memory = NULL;

  GST_OBJECT_LOCK (self->sink);
  memory = gst_dshowvideo_sink_allocator_alloc_locked (self, size, params);
  GST_OBJECT_UNLOCK (self->sink);

  if (!memory) {
	  memory = gst_allocator_alloc(NULL, size, params);
	  GST_LOG_OBJECT(self,
		  "Not enough shared memory for GstMemory of %" G_GSIZE_FORMAT
		  "bytes, allocating using standard allocator", size);
  }

  return memory;
}


static void
gst_shm_sink_allocator_class_init (GstShmSinkAllocatorClass * klass)
{

  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  allocator_class->alloc = gst_dshowvideo_sink_allocator_alloc;
  allocator_class->free = gst_shm_sink_allocator_free;
  object_class->dispose = gst_shm_sink_allocator_dispose;
}

static GstShmSinkAllocator *
gst_shm_sink_allocator_new (GstShmSink * sink)
{
	DebugBreak();
  GstShmSinkAllocator *self = g_object_new (GST_TYPE_SHM_SINK_ALLOCATOR, NULL);

  self->sink = gst_object_ref (sink);

  return self;
}


/***************
 * MAIN OBJECT *
 ***************/

#define ALIGN(nr, align) \
 (nr % align == 0) ? nr : align * (nr / align +1)

void get_buffer_size(size_t *buffer_size, GstVideoFormat format, uint32_t width, uint32_t height, uint32_t alignment) {
    // FIXME  - fix hard coded 720p
    size_t size = 1382400;   // i420 720p
    // reading beyond buffer size is a thing for video buffers - so we need to make the backing buffer a bit bigger
    size += alignment;
    *buffer_size = ALIGN(size, alignment);
}

void get_frame_size(size_t *frame_size, size_t *frame_data_offset, GstVideoFormat format, uint32_t width, uint32_t height, uint32_t alignment) {
    // FIXME  - fix hard coded 720p

    size_t buffer_size;
    get_buffer_size(&buffer_size, format, width, height, alignment);
    size_t header_size = ALIGN(sizeof(struct frame_header), alignment);

    *frame_size = buffer_size + header_size;
    *frame_data_offset = header_size;
}

#define ALIGNMENT 256
static void
gst_shm_sink_init (GstShmSink * self)
{
  g_cond_init (&self->cond);
  //self->size = DEFAULT_SIZE;
  self->wait_for_connection = DEFAULT_WAIT_FOR_CONNECTION;

  self->shmem_mutex = CreateMutexW(NULL, true, BEBO_SHMEM_MUTEX);
  // FIXME handle creation error
  self->shmem_new_data_semaphore = CreateSemaphoreW(NULL, 0, 1, BEBO_SHMEM_DATA_SEM);
  // FIXME handle creation error

  DWORD size = 0;
  DWORD header_size = ALIGN(sizeof(struct shmem), ALIGNMENT);
  int buffer_count = 1;
  // FIXME  - fix hard coded 720p
  size_t frame_data_offset;
  size_t frame_size;
  get_frame_size(&frame_size, &frame_data_offset, GST_VIDEO_FORMAT_I420, 1280, 720, ALIGNMENT);
  size = ((DWORD) frame_size * buffer_count) + header_size;

  self->shmem_handle = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
        0, size, BEBO_SHMEM_NAME);

  if (!self->shmem_handle) {
      // FIXME should be ERROR
      GST_WARNING_OBJECT(self, "could not create mapping %d", GetLastError());
      return;
  }
  self->shmem = MapViewOfFile(self->shmem_handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
  if (!self->shmem) {
      // FIXME should be ERROR
      GST_WARNING_OBJECT(self, "could not map shmem %d", GetLastError());
      return;
  }

  gst_video_info_set_format(&self->shmem->video_info, GST_VIDEO_FORMAT_I420, 1280, 720);

//  for (int i = 0; i < GST_VIDEO_MAX_PLANES; ++i) {
//      self->shmem->video_info.plane_offsets[i] = GST_VIDEO_INFO_PLANE_OFFSET(&(self->shmem->video_info), i);
//      self->shmem->video_info.plane_strides[i] = GST_VIDEO_INFO_PLANE_STRIDE(&(self->shemem->video_info), i);
//  }

  self->shmem->frame_offset = header_size;
  self->shmem->frame_size = frame_size;
  self->shmem->buffer_size = frame_size - header_size;
  self->shmem->count = buffer_count;
  self->shmem->write_ptr = 0;
  self->shmem->read_ptr = 0;
  self->shmem->frame_data_offset = frame_data_offset;
  self->shmem->shmem_size = size;

  ReleaseMutex(self->shmem_mutex);

  //gst_allocation_params_init (&self->params);
}

static void
gst_shm_sink_class_init (GstShmSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;

  gobject_class->finalize = gst_shm_sink_finalize;
  gobject_class->set_property = gst_shm_sink_set_property;
  gobject_class->get_property = gst_shm_sink_get_property;

  gstbasesink_class->start = GST_DEBUG_FUNCPTR (gst_shm_sink_start);
  gstbasesink_class->stop = GST_DEBUG_FUNCPTR (gst_shm_sink_stop);
  gstbasesink_class->render = GST_DEBUG_FUNCPTR (gst_shm_sink_render);
  gstbasesink_class->event = GST_DEBUG_FUNCPTR (gst_shm_sink_event);
  gstbasesink_class->unlock = GST_DEBUG_FUNCPTR (gst_shm_sink_unlock);
  gstbasesink_class->unlock_stop = GST_DEBUG_FUNCPTR (gst_shm_sink_unlock_stop);
  gstbasesink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_shm_sink_propose_allocation);




  g_object_class_install_property (gobject_class, PROP_WAIT_FOR_CONNECTION,
      g_param_spec_boolean ("wait-for-connection",
          "Wait for a connection until rendering",
          "Block the stream until the shm pipe is connected",
          DEFAULT_WAIT_FOR_CONNECTION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_BUFFER_TIME,
      g_param_spec_int64 ("buffer-time",
          "Buffer Time of the shm buffer",
          "Maximum Size of the shm buffer in nanoseconds (-1 to disable)",
          -1, G_MAXINT64, -1,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  signals[SIGNAL_CLIENT_CONNECTED] = g_signal_new ("client-connected",
      GST_TYPE_SHM_SINK, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

  signals[SIGNAL_CLIENT_DISCONNECTED] = g_signal_new ("client-disconnected",
      GST_TYPE_SHM_SINK, G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__INT, G_TYPE_NONE, 1, G_TYPE_INT);

  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);

  gst_element_class_set_static_metadata (gstelement_class,
      "Direct Show Filter Sink",
      "Sink",
      "Send data over shared memory to gst-to-dshow DirectShow Filter Source",
      "Florian P. Nierhaus <fpn@bebo.com>");

  GST_DEBUG_CATEGORY_INIT (shmsink_debug, "dshowfiltersink", 0, "DirectShow Filter Sink");
}

static void
gst_shm_sink_finalize (GObject * object)
{
  GstShmSink *self = GST_SHM_SINK (object);

  g_cond_clear (&self->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/*
 * Set the value of a property for the server sink.
 */
static void
gst_shm_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstShmSink *self = GST_SHM_SINK (object);
  int ret = 0;

  switch (prop_id) {
#if 0      
    case PROP_SOCKET_PATH:
      GST_OBJECT_LOCK (object);
      g_free (self->socket_path);
      self->socket_path = g_value_dup_string (value);
      GST_OBJECT_UNLOCK (object);
      break;
    case PROP_PERMS:
      GST_OBJECT_LOCK (object);
      self->perms = g_value_get_uint (value);
      if (self->pipe)
        ret = sp_writer_setperms_shm (self->pipe, self->perms);
      GST_OBJECT_UNLOCK (object);
      if (ret < 0)
        GST_WARNING_OBJECT (object, "Could not set permissions on pipe: %s",
            strerror (ret));
      break;
    case PROP_SHM_SIZE:
      GST_OBJECT_LOCK (object);
      if (self->pipe) {
        if (sp_writer_resize (self->pipe, g_value_get_uint (value)) < 0) {
          /* Swap allocators, so we can know immediately if the memory is
           * ours */
          gst_object_unref (self->allocator);
          self->allocator = gst_shm_sink_allocator_new (self);

          GST_DEBUG_OBJECT (self, "Resized shared memory area from %u to "
              "%u bytes", self->size, g_value_get_uint (value));
        } else {
          GST_WARNING_OBJECT (self, "Could not resize shared memory area from"
              "%u to %u bytes", self->size, g_value_get_uint (value));
        }
      }
      self->size = g_value_get_uint (value);
      */
      GST_OBJECT_UNLOCK (object);
      break;
#endif 
    case PROP_WAIT_FOR_CONNECTION:
      GST_OBJECT_LOCK (object);
      self->wait_for_connection = g_value_get_boolean (value);
      GST_OBJECT_UNLOCK (object);
      g_cond_broadcast (&self->cond);
      break;
    case PROP_BUFFER_TIME:
      GST_OBJECT_LOCK (object);
      self->buffer_time = g_value_get_int64 (value);
      GST_OBJECT_UNLOCK (object);
      g_cond_broadcast (&self->cond);
      break;
    default:
      break;
  }
}

static void
gst_shm_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstShmSink *self = GST_SHM_SINK (object);

  GST_OBJECT_LOCK (object);

  switch (prop_id) {
  
    case PROP_WAIT_FOR_CONNECTION:
      g_value_set_boolean (value, self->wait_for_connection);
      break;
    case PROP_BUFFER_TIME:
      g_value_set_int64 (value, self->buffer_time);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

  GST_OBJECT_UNLOCK (object);
}



static gboolean
gst_shm_sink_start (GstBaseSink * bsink)
{
  GstShmSink *self = GST_SHM_SINK (bsink);
  GError *err = NULL;

  self->stop = FALSE;

  self->allocator = gst_shm_sink_allocator_new(self);


  return TRUE;
}


static gboolean
gst_shm_sink_stop (GstBaseSink * bsink)
{
  GstShmSink *self = GST_SHM_SINK (bsink);

  self->stop = TRUE;


  if (self->allocator)
    gst_object_unref (self->allocator);
  self->allocator = NULL;


  GST_DEBUG_OBJECT (self, "Stopping");

#if 0
  while (self->clients) {
    struct GstShmClient *client = self->clients->data;
    self->clients = g_list_remove (self->clients, client);
    sp_writer_close_client (self->pipe, client->client,
        (sp_buffer_free_callback) gst_buffer_unref, NULL);
    g_signal_emit (self, signals[SIGNAL_CLIENT_DISCONNECTED], 0,
        client->pollfd.fd);
    g_slice_free (struct GstShmClient, client);
  }

  gst_poll_free (self->poll);
  self->poll = NULL;

  sp_writer_close (self->pipe, NULL, NULL);
  self->pipe = NULL;
#endif

  return TRUE;
}

static gboolean
gst_shm_sink_can_render (GstShmSink * self, GstClockTime time)
{
//  ShmBuffer *b;

  if (time == GST_CLOCK_TIME_NONE || self->buffer_time == GST_CLOCK_TIME_NONE)
    return TRUE;

#if 0
  b = sp_writer_get_pending_buffers (self->pipe);
  for (; b != NULL; b = sp_writer_get_next_buffer (b)) {
    GstBuffer *buf = sp_writer_buf_get_tag (b);
    if (GST_CLOCK_DIFF (time, GST_BUFFER_PTS (buf)) > self->buffer_time)
      return FALSE;
  }
#endif

  return TRUE;
}

static GstFlowReturn
gst_shm_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
    GstShmSink *self = GST_SHM_SINK (bsink);
    // TODO
    //GST_WARNING_OBJECT (self, "we need to implemen sink_render ! Buffer %p has %d GstMemory size: %d", buf,
    //     gst_buffer_n_memory (buf),
    //    gst_buffer_get_size(buf));

    if (!GST_IS_BUFFER(buf)) {
        GST_WARNING_OBJECT(self, "NOT A BUFFER???");
    }
    GST_OBJECT_LOCK (self);

    DWORD rc = WaitForSingleObject(self->shmem_mutex, INFINITE);
    if (rc == WAIT_FAILED) {
        GST_WARNING_OBJECT(self, "MUTEX ERROR %#010x", GetLastError());
    } else if (rc != WAIT_OBJECT_0) {
        GST_WARNING_OBJECT(self, "WTF MUTEX %#010x", rc);
    }
    
    uint64_t i = self->shmem->write_ptr % self->shmem->count;
    self->shmem->write_ptr++;

    uint64_t frame_offset =  self->shmem->frame_offset +  i * self->shmem->frame_size;
    uint64_t data_offset = self->shmem->frame_data_offset;

    struct frame_header *frame = ((struct frame_header*) (((unsigned char*)self->shmem) + frame_offset));
    void *data = ((char*)frame) + data_offset;

    GstMapInfo map;
    GstMemory *memory = NULL;

    gst_buffer_map(buf, &map, GST_MAP_READ);
    frame->dts = buf->dts;
    frame->pts = buf->pts;
    frame->duration = buf->duration;
    frame->discontinuity = GST_BUFFER_IS_DISCONT(buf);
//    gsize size = gst_buffer_extract(buf, 0, data, self->shmem->buffer_size);
    gst_buffer_unmap(buf, &map);
#if 0
    GST_DEBUG_OBJECT(self, "pts: %lld i: %d frame_offset: %d offset: data_offset: %d size: %d",
        //frame->dts / 1000000,
        frame->pts / 1000000,
        i,
        frame_offset,
        data_offset,
        size);
#endif
    ReleaseMutex(self->shmem_mutex);
    GST_OBJECT_UNLOCK (self);
    ReleaseSemaphore(self->shmem_new_data_semaphore, 1, NULL);

    return GST_FLOW_OK;

#if 0
  int rv = 0;
  GstMapInfo map;
  gboolean need_new_memory = FALSE;
  GstFlowReturn ret = GST_FLOW_OK;
  GstMemory *memory = NULL;
  GstBuffer *sendbuf = NULL;

  GST_OBJECT_LOCK (self);
  while (self->wait_for_connection && !self->clients) {
    g_cond_wait (&self->cond, GST_OBJECT_GET_LOCK (self));
    if (self->unlock)
      goto flushing;
  }

  while (!gst_shm_sink_can_render (self, GST_BUFFER_TIMESTAMP (buf))) {
    g_cond_wait (&self->cond, GST_OBJECT_GET_LOCK (self));
    if (self->unlock)
      goto flushing;
  }


  if (gst_buffer_n_memory (buf) > 1) {
    GST_LOG_OBJECT (self, "Buffer %p has %d GstMemory, we only support a single"
        " one, need to do a memcpy", buf, gst_buffer_n_memory (buf));
    need_new_memory = TRUE;
  } else {
    memory = gst_buffer_peek_memory (buf, 0);

    if (memory->allocator != GST_ALLOCATOR (self->allocator)) {
      need_new_memory = TRUE;
      GST_LOG_OBJECT (self, "Memory in buffer %p was not allocated by "
          "%" GST_PTR_FORMAT ", will memcpy", buf, memory->allocator);
    }
  }

  if (need_new_memory) {
    if (gst_buffer_get_size (buf) > sp_writer_get_max_buf_size (self->pipe)) {
      gsize area_size = sp_writer_get_max_buf_size (self->pipe);
      GST_OBJECT_UNLOCK (self);
      GST_ELEMENT_ERROR (self, RESOURCE, NO_SPACE_LEFT,
          ("Shared memory area is too small"),
          ("Shared memory area of size %" G_GSIZE_FORMAT " is smaller than"
              "buffer of size %" G_GSIZE_FORMAT, area_size,
              gst_buffer_get_size (buf)));
      return GST_FLOW_ERROR;
    }

    while ((memory =
            gst_shm_sink_allocator_alloc_locked (self->allocator,
                gst_buffer_get_size (buf), &self->params)) == NULL) {
      g_cond_wait (&self->cond, GST_OBJECT_GET_LOCK (self));
      if (self->unlock)
        goto flushing;
    }

    while (self->wait_for_connection && !self->clients) {
      g_cond_wait (&self->cond, GST_OBJECT_GET_LOCK (self));
      if (self->unlock) {
        GST_OBJECT_UNLOCK (self);
        gst_memory_unref (memory);
        return GST_FLOW_FLUSHING;
      }
    }

    gst_memory_map (memory, &map, GST_MAP_WRITE);
    gst_buffer_extract (buf, 0, map.data, map.size);
    gst_memory_unmap (memory, &map);

    sendbuf = gst_buffer_new ();
    gst_buffer_copy_into (sendbuf, buf, GST_BUFFER_COPY_METADATA, 0, -1);
    gst_buffer_append_memory (sendbuf, memory);
  } else {
    sendbuf = gst_buffer_ref (buf);
  }

  gst_buffer_map (sendbuf, &map, GST_MAP_READ);
  /* Make the memory readonly as of now as we've sent it to the other side
   * We know it's not mapped for writing anywhere as we just mapped it for
   * reading
   */

  rv = sp_writer_send_buf (self->pipe, (char *) map.data, map.size, sendbuf);

  gst_buffer_unmap (sendbuf, &map);

  GST_OBJECT_UNLOCK (self);

  if (rv == 0) {
    GST_DEBUG_OBJECT (self, "No clients connected, unreffing buffer");
    gst_buffer_unref (sendbuf);
  } else if (rv == -1) {
    GST_ELEMENT_ERROR (self, STREAM, FAILED, ("Invalid allocated buffer"),
        ("The shmpipe library rejects our buffer, this is a bug"));
    ret = GST_FLOW_ERROR;
  }

  /* If we allocated our own memory, then unmap it */

  return ret;

flushing:
  GST_OBJECT_UNLOCK (self);
#endif

  return GST_FLOW_FLUSHING;
}

static void
free_buffer_locked (GstBuffer * buffer, void *data)
{
  GSList **list = data;

  g_assert (buffer != NULL);

  *list = g_slist_prepend (*list, buffer);
}


static gboolean
gst_shm_sink_event (GstBaseSink * bsink, GstEvent * event)
{
  GstShmSink *self = GST_SHM_SINK (bsink);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_OBJECT_LOCK (self);
      // TODO
#if 0
      while (self->wait_for_connection && sp_writer_pending_writes (self->pipe)
          && !self->unlock)
        g_cond_wait (&self->cond, GST_OBJECT_GET_LOCK (self));
#endif
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      break;
  }

  return GST_BASE_SINK_CLASS (parent_class)->event (bsink, event);
}


static gboolean
gst_shm_sink_unlock (GstBaseSink * bsink)
{
  GstShmSink *self = GST_SHM_SINK (bsink);

  GST_OBJECT_LOCK (self);
  self->unlock = TRUE;
  GST_OBJECT_UNLOCK (self);

  g_cond_broadcast (&self->cond);
  return TRUE;
}

static gboolean
gst_shm_sink_unlock_stop (GstBaseSink * bsink)
{
  GstShmSink *self = GST_SHM_SINK (bsink);

  GST_OBJECT_LOCK (self);
  self->unlock = FALSE;
  GST_OBJECT_UNLOCK (self);

  return TRUE;
}

static gboolean
gst_shm_sink_propose_allocation (GstBaseSink * sink, GstQuery * query)
{
  GstShmSink *self = GST_SHM_SINK (sink);

  //DebugBreak();

  if (self->allocator)
  {
	  //GST_DEBUG_LOG(self, "-------> gst_shm_sink_propose_allocation");

	  gst_query_add_allocation_param(query, GST_ALLOCATOR(self->allocator),
		  NULL);
  }


  return TRUE;
}