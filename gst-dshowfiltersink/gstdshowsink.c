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
  PROP_SOCKET_PATH,
  PROP_PERMS,
  PROP_SHM_SIZE,
  PROP_WAIT_FOR_CONNECTION,
  PROP_BUFFER_TIME
};

#define DEFAULT_SIZE ( 64 * 1024 * 1024 )
#define DEFAULT_WAIT_FOR_CONNECTION (FALSE)
/* Default is user read/write, group read */
#define DEFAULT_PERMS ( S_IRUSR | S_IWUSR | S_IRGRP )
#define ALIGNMENT 256

GST_DEBUG_CATEGORY_STATIC (shmsink_debug);
#define GST_CAT_DEFAULT shmsink_debug

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

/***************
 * MAIN OBJECT *
 ***************/

#define ALIGN(nr, align) \
  (nr % align == 0) ? nr : align * (nr / align +1)

static void 
get_buffer_size (size_t *buffer_size, GstVideoFormat format, uint32_t width, uint32_t height, uint32_t alignment) {
  size_t size = width * height * 3 / 2;   // i420
  // reading beyond buffer size is a thing for v
  size += alignment;
  *buffer_size = ALIGN(size, alignment);
}

static void 
get_frame_size (size_t *frame_size, size_t *frame_data_offset, GstVideoFormat format, uint32_t width, uint32_t height, uint32_t alignment) {
  size_t buffer_size;
  get_buffer_size(&buffer_size, format, width, height, alignment);
  size_t header_size = ALIGN(sizeof(struct frame_header), alignment);

  *frame_size = buffer_size + header_size;
  *frame_data_offset = header_size;
}

static void 
init_shmem (GstShmSink * self, GstVideoInfo * info) {
  // FIXME handle creation error
  self->shmem_mutex = CreateMutexW(NULL, true, BEBO_SHMEM_MUTEX);
  self->shmem_new_data_semaphore = CreateSemaphoreW(NULL, 0, 1, BEBO_SHMEM_DATA_SEM);

  DWORD size = 0;
  DWORD header_size = ALIGN(sizeof(struct shmem), ALIGNMENT);
  int buffer_count = 5;

  // FIXME  - fix hard coded 720p
  size_t frame_data_offset;
  size_t frame_size;

  get_frame_size(&frame_size, &frame_data_offset, 
      GST_VIDEO_INFO_FORMAT(info), 
      GST_VIDEO_INFO_WIDTH(info),
      GST_VIDEO_INFO_HEIGHT(info),
      ALIGNMENT);
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

  gst_video_info_set_format(&self->shmem->video_info, 
      GST_VIDEO_INFO_FORMAT(info), 
      GST_VIDEO_INFO_WIDTH(info),
      GST_VIDEO_INFO_HEIGHT(info));

  self->shmem->frame_offset = header_size;
  self->shmem->frame_size = frame_size;
  self->shmem->buffer_size = frame_size - header_size;
  self->shmem->count = buffer_count;
  self->shmem->write_ptr = 0;
  self->shmem->read_ptr = 0;
  self->shmem->frame_data_offset = frame_data_offset;
  self->shmem->shmem_size = size;

  ReleaseMutex(self->shmem_mutex);
  // self->perms = DEFAULT_PERMS;
  // gst_allocation_params_init (&self->params);
}

static void
gst_shm_sink_init (GstShmSink * self)
{
  g_cond_init (&self->cond);
  self->size = DEFAULT_SIZE;
  self->wait_for_connection = DEFAULT_WAIT_FOR_CONNECTION;
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

  g_object_class_install_property (gobject_class, PROP_SOCKET_PATH,
      g_param_spec_string ("socket-path",
        "Path to the control socket",
        "The path to the control socket used to control the shared memory "
        "transport. This may be modified during the NULL->READY transition",
        NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

#if 0
  g_object_class_install_property (gobject_class, PROP_PERMS,
      g_param_spec_uint ("perms",
        "Permissions on the shm area",
        "Permissions to set on the shm area",
        0, 07777, DEFAULT_PERMS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

  g_object_class_install_property (gobject_class, PROP_SHM_SIZE,
      g_param_spec_uint ("shm-size",
        "Size of the shm area",
        "Size of the shared memory area",
        0, G_MAXUINT, DEFAULT_SIZE,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
  g_free (self->socket_path);

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
    case PROP_SOCKET_PATH:
      g_value_set_string (value, self->socket_path);
      break;
    case PROP_PERMS:
      g_value_set_uint (value, self->perms);
      break;
    case PROP_SHM_SIZE:
      g_value_set_uint (value, self->size);
      break;
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

  GST_DEBUG_OBJECT (self, "Creating new socket at %s"
      " with shared memory of %d bytes", self->socket_path, self->size);
  return TRUE;
}


static gboolean
gst_shm_sink_stop (GstBaseSink * bsink)
{
  GstShmSink *self = GST_SHM_SINK (bsink);

  self->stop = TRUE;
  gst_poll_set_flushing (self->poll, TRUE);

  if (self->allocator)
    gst_object_unref (self->allocator);
  self->allocator = NULL;

  g_thread_join (self->pollthread);
  self->pollthread = NULL;

  GST_DEBUG_OBJECT (self, "Stopping");

  return TRUE;
}

static gboolean
gst_shm_sink_can_render (GstShmSink * self, GstClockTime time)
{
  if (time == GST_CLOCK_TIME_NONE || self->buffer_time == GST_CLOCK_TIME_NONE)
    return TRUE;

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
  gsize size = gst_buffer_extract(buf, 0, data, self->shmem->buffer_size);
  gst_buffer_unmap(buf, &map);

  GST_DEBUG_OBJECT(self, "pts: %lld i: %d frame_offset: %d offset: data_offset: %d size: %d",
      //frame->dts / 1000000,
      frame->pts / 1000000,
      i,
      frame_offset,
      data_offset,
      size);

  ReleaseMutex(self->shmem_mutex);
  GST_OBJECT_UNLOCK (self);
  ReleaseSemaphore(self->shmem_new_data_semaphore, 1, NULL);

  return GST_FLOW_OK;
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

#if 0 // do we want our own allocator?
  if (self->allocator)
    gst_query_add_allocation_param (query, GST_ALLOCATOR (self->allocator),
        NULL);
#endif

  GstCaps *caps;
  gboolean need_pool;
  gst_query_parse_allocation(query, &caps, &need_pool);

  GstVideoInfo info;
  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  init_shmem(self, &info);

  return TRUE;
}
