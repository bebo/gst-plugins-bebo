/* GStreamer
 * Copyright (C) <2018> Pigs in Flight, Inc (Bebo)
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
 *
 * vim: ts=2:sw=2
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <windows.h>
#include <gst/gst.h>
#include <gst/gl/gl.h>

#include <GL/glext.h>
#include <GL/wglext.h>

#include "gstdshowsink.h"
#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>
#include "../shared/bebo_shmem.h"

#ifdef NDEBUG
#undef GST_LOG_OBJECT
#define GST_LOG_OBJECT(...)
#endif


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
  PROP_BUFFER_TIME,
  PROP_LATENCY
};


#define SUPPORTED_GL_APIS (GST_GL_API_OPENGL3)
#define DEFAULT_WAIT_FOR_CONNECTION (FALSE)
#define BUFFER_COUNT 30

GST_DEBUG_CATEGORY_STATIC (shmsink_debug);
#define GST_CAT_DEFAULT shmsink_debug

#define GST_GL_SINK_CAPS \
    "video/x-raw(" GST_CAPS_FEATURE_MEMORY_GL_MEMORY "), "              \
    "format = (string) RGBA, "                                          \
    "width = " GST_VIDEO_SIZE_RANGE ", "                                \
    "height = " GST_VIDEO_SIZE_RANGE ", "                               \
    "framerate = " GST_VIDEO_FPS_RANGE ", "                             \
    "texture-target = (string) { 2D, external-oes }"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_SINK_CAPS));

#define parent_class gst_shm_sink_parent_class 
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

static void 
gst_dshowfiltersink_set_context(GstElement *element,
    GstContext *context) {
  GstShmSink *self = GST_SHM_SINK (element);

  gst_gl_handle_set_context(element, context,
      &self->display, &self->other_context);

  if (self->display) {
    gst_gl_display_filter_gl_api (self->display, SUPPORTED_GL_APIS);
  }

  GST_ELEMENT_CLASS(gst_shm_sink_parent_class)->set_context(element, context);
}


/***************
 * MAIN OBJECT *
 ***************/

#define ALIGN(nr, align) \
 (nr % align == 0) ? nr : align * (nr / align +1)

#define ALIGNMENT 64

static void
initialize_shared_memory(GstShmSink * self, uint64_t width, uint64_t height, uint64_t fps)
{
  DWORD size = 0;
  DWORD header_size = ALIGN(sizeof(struct shmem), ALIGNMENT);
  self->shmem_mutex = CreateMutexW(NULL, true, BEBO_SHMEM_MUTEX);

  size_t frame_size = ALIGN(sizeof(struct frame), ALIGNMENT);
  size = ((DWORD)frame_size * BUFFER_COUNT) + header_size;

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

  gst_video_info_set_format(&self->shmem->video_info, GST_VIDEO_FORMAT_I420, width, height);
  GstVideoInfo * info = &self->shmem->video_info;
  info->fps_n = fps;

  self->shmem->version = SHM_INTERFACE_VERSION;
  self->shmem->frame_offset = header_size;
  self->shmem->frame_size = frame_size;
  self->shmem->count = BUFFER_COUNT;
  self->shmem->write_ptr = 0;
  self->shmem->read_ptr = 0;
  self->shmem->shmem_size = size;

  ReleaseMutex(self->shmem_mutex);
}

static void
gst_shm_sink_init (GstShmSink * self)
{
  self->first_render_time = 0;
  self->latency = -1;
  g_cond_init (&self->cond);
  //self->size = DEFAULT_SIZE;
  self->wait_for_connection = DEFAULT_WAIT_FOR_CONNECTION;

  // FIXME handle creation error
  self->shmem_new_data_semaphore = CreateSemaphoreW(NULL, 0, 1, BEBO_SHMEM_DATA_SEM);
  // FIXME handle creation error

  gst_allocation_params_init (&self->params);
}

static void
gst_shm_sink_class_init (GstShmSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS(klass);
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

  gstelement_class->set_context = GST_DEBUG_FUNCPTR (gst_dshowfiltersink_set_context);
  // FIXME: should we implement gst_element_change_state();

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

  g_object_class_install_property(gobject_class, PROP_LATENCY,
    g_param_spec_int64("latency", "latency", "The pipeline's measured latency",
      0, G_MAXINT64, 3, G_PARAM_READWRITE));

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
    case PROP_LATENCY:
      self->latency = g_value_get_int64 (value);
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
    case PROP_LATENCY:
      g_value_set_int64(value, self->latency / 1000000);
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

  gst_gl_ensure_element_data (GST_ELEMENT (self),
        (GstGLDisplay **) & self->display,
        (GstGLContext **) & self->other_context);
  self->allocator = gst_gl_dxgi_memory_allocator_new(self);

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

static bool latency_bool = TRUE;
static GstFlowReturn
gst_shm_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
    GstShmSink *self = GST_SHM_SINK (bsink);
    GST_DEBUG_OBJECT(self, "gst_shm_sink_render");


    if (!GST_IS_BUFFER(buf)) {
        GST_ERROR_OBJECT(self, "NOT A BUFFER???");
        return GST_FLOW_ERROR;
    }
    GST_OBJECT_LOCK (self);

    /* GST_OBJECT_LOCK (self); */
    GstClock *clock = GST_ELEMENT_CLOCK (self);
    if (clock != NULL) {
        gst_object_ref (clock);
    }

    GstClockTime running_time = 0;
    GstClockTime latency = 0;
    if (clock != NULL) {

      /* The time according to the current clock */
      GstClockTime base_time = GST_ELEMENT_CAST (bsink)->base_time;
      running_time = gst_clock_get_time(clock) - base_time;
      latency = running_time - buf->pts;
      if (latency_bool) {
        self->latency = latency;
        latency_bool = FALSE;
        GST_INFO("Measured plugin latency to %d", self->latency / 1000000);
      }

      /* GST_DEBUG_OBJECT (self, */
      /*   "pts %" GST_TIME_FORMAT ", running_time %" GST_TIME_FORMAT ", base_time %" GST_TIME_FORMAT ", latency %" GST_TIME_FORMAT, */
      /*   GST_TIME_ARGS (buf->pts), GST_TIME_ARGS(running_time), GST_TIME_ARGS (base_time), GST_TIME_ARGS(latency)); */

      gst_object_unref (clock);
      clock = NULL;
    }
    // we get bombarded with "old" frames at the beginning - drop them for now
    if (self->first_render_time == 0) {
        self->first_render_time  = running_time;
    }
    if (GST_BUFFER_DTS_OR_PTS(buf) < self->first_render_time) {
        GST_OBJECT_UNLOCK (self);
        GST_DEBUG_OBJECT(self, "dropping early frame");
        return GST_FLOW_OK;
    }

    DWORD rc = WaitForSingleObject(self->shmem_mutex, 16);

    if (rc == WAIT_FAILED) {
        GST_WARNING_OBJECT(self, "MUTEX ERROR %#010x", GetLastError());
    } else if (rc == WAIT_TIMEOUT) {
        // FIXME: TODO log dropped frames
        GST_DEBUG_OBJECT(self, "MUTEX TIMED OUT dropping frame");
        GST_OBJECT_UNLOCK (self);
        return GST_FLOW_OK;
    } else if (rc != WAIT_OBJECT_0) {
        GST_WARNING_OBJECT(self, "WTF MUTEX %#010x", rc);
        GST_OBJECT_UNLOCK (self);
        return GST_FLOW_OK;
    }

    self->shmem->write_ptr++;
    uint64_t index = self->shmem->write_ptr % self->shmem->count;

    uint64_t frame_offset =  self->shmem->frame_offset +  index * self->shmem->frame_size;
    struct frame *frame = ((struct frame*) (((unsigned char*)self->shmem) + frame_offset));

    if (frame->_gst_buf_ref != NULL) {
      if (frame->ref_cnt > 0) {
        GST_DEBUG_OBJECT(self,
          "gst_buffer_unref(%p) no free buffer unread buffer - freeing anyway - index: %d dxgi_handle: %llu ref_cnt: %d",
          frame->_gst_buf_ref,
          index,
          frame->dxgi_handle,
          frame->ref_cnt);

        self->shmem->write_ptr--;
        ReleaseMutex(self->shmem_mutex);
        GST_OBJECT_UNLOCK(self);
        ReleaseSemaphore(self->shmem_new_data_semaphore, 1, NULL);
        return GST_FLOW_OK;
      }
      gst_buffer_unref(frame->_gst_buf_ref);
      frame->ref_cnt = 0;
      frame->_gst_buf_ref = NULL;
    }

    /* GstMapInfo map; */
    GstMemory *memory = gst_buffer_peek_memory(buf, 0);

    if (memory->allocator != GST_ALLOCATOR(self->allocator)) {
        GST_ERROR_OBJECT(self, "Memory in buffer %p was not allocated by us: "
            "%" GST_PTR_FORMAT ", will memcpy", buf, memory->allocator);
    }


    GstGLDXGIMemory * gl_dxgi_mem = (GstGLDXGIMemory *) memory;

    // We don't map memory here as we don't want to lock D3D11
    // TODO: or should we?
    frame->latency = latency;
    frame->dts = buf->dts;
    frame->pts = buf->pts;
    frame->duration = buf->duration;
    frame->discontinuity = GST_BUFFER_IS_DISCONT(buf);
    frame->size = gst_buffer_get_size(buf);
    frame->dxgi_handle = gl_dxgi_mem->dxgi_handle;
    frame->nr = self->shmem->write_ptr;
    frame->_gst_buf_ref = buf;
    frame->ref_cnt = 1;
    gst_buffer_ref (buf);

    GST_DEBUG_OBJECT(self, "nr: %llu dxgi_handle: %llu pts: %lld i: %d frame_offset: %d size: %d buf: %p latency: %d",
        frame->nr,
        frame->dxgi_handle,
        //frame->dts / 1000000,
        frame->pts / 1000000,
        index,
        frame_offset,
        frame->size,
        buf,
        frame->latency / 1000000
        ); //size);

    for (int i=0 ; i < self->shmem->count ; i++) {
      uint64_t frame_offset =  self->shmem->frame_offset +  i * self->shmem->frame_size;
      struct frame *frame = ((struct frame*) (((unsigned char*)self->shmem) + frame_offset));
      if (frame->_gst_buf_ref != NULL && frame->ref_cnt == 0) {
        gst_buffer_unref(frame->_gst_buf_ref);
        memset(frame, 0, sizeof(struct frame));
      }
    };

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

const static D3D_FEATURE_LEVEL d3d_feature_levels[] =
{
  D3D_FEATURE_LEVEL_11_0,
  D3D_FEATURE_LEVEL_10_1,
  D3D_FEATURE_LEVEL_10_0,
  D3D_FEATURE_LEVEL_9_3,
};

static ID3D11Device* create_device_d3d11() {
  // TODO: should I be using ComPtr here?
  ID3D11Device *device;
  // ID3D11DeviceContext *context;
  // IDXGIAdapter *adapter;

  /* D3D_FEATURE_LEVEL level_used = D3D_FEATURE_LEVEL_9_3; */
  D3D_FEATURE_LEVEL level_used = D3D_FEATURE_LEVEL_10_1;

  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#ifdef _DEBUG
  flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  HRESULT hr = D3D11CreateDevice(
      NULL, 
      D3D_DRIVER_TYPE_HARDWARE,
      NULL, 
      flags, 
      d3d_feature_levels,
      sizeof(d3d_feature_levels) / sizeof(D3D_FEATURE_LEVEL),
      D3D11_SDK_VERSION,
      &device,
      &level_used, 
      NULL);

  GST_INFO("CreateDevice HR: 0x%08x, level_used: 0x%08x (%d)", hr,
      (unsigned int) level_used, (unsigned int) level_used);

  return device;
}

static void init_wgl_functions(GstGLContext* gl_context, GstDXGID3D11Context *share_context) {
  GST_INFO("GL_VENDOR  : %s", glGetString(GL_VENDOR));
  GST_INFO("GL_VERSION : %s", glGetString(GL_VERSION));
  share_context->wglDXOpenDeviceNV = (PFNWGLDXOPENDEVICENVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXOpenDeviceNV");
  share_context->wglDXCloseDeviceNV = (PFNWGLDXCLOSEDEVICENVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXCloseDeviceNV");
  share_context->wglDXRegisterObjectNV = (PFNWGLDXREGISTEROBJECTNVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXRegisterObjectNV");
  share_context->wglDXUnregisterObjectNV = (PFNWGLDXUNREGISTEROBJECTNVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXUnregisterObjectNV");
  share_context->wglDXLockObjectsNV = (PFNWGLDXLOCKOBJECTSNVPROC) 
    gst_gl_context_get_proc_address(gl_context, "wglDXLockObjectsNV");
  share_context->wglDXUnlockObjectsNV = (PFNWGLDXUNLOCKOBJECTSNVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXUnlockObjectsNV");
  share_context->wglDXSetResourceShareHandleNV = (PFNWGLDXSETRESOURCESHAREHANDLENVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXSetResourceShareHandleNV");
}

static void init_d3d11_context(GstGLContext* gl_context, gpointer * sink) {

  GstShmSink *self = GST_SHM_SINK (sink);

  GstDXGID3D11Context *share_context = g_new(GstDXGID3D11Context, 1);
  init_wgl_functions(gl_context, share_context);
  share_context->d3d11_device = create_device_d3d11();
  g_assert( share_context->d3d11_device != NULL);
  share_context->device_interop_handle = share_context->wglDXOpenDeviceNV(share_context->d3d11_device);
  g_assert( share_context->device_interop_handle != NULL);
  g_object_set_data(gl_context, GST_GL_DXGI_D3D11_CONTEXT, share_context);
  // FIXME: how do we close these???
}

static gboolean
gst_dshow_filter_sink_ensure_gl_context(GstShmSink * self) {
  GError *error = NULL;

  if (self->context && true) {
    //FIXME check has dxgi and if not -> remove and add?
    return TRUE;
  }


  if (! self->context) {
    gst_gl_ensure_element_data (GST_ELEMENT (self),
          (GstGLDisplay **) & self->display,
          (GstGLContext **) & self->other_context);
  }
  if (!self->context) {
    GST_OBJECT_LOCK (self->display);
    do {
      if (self->context) {
        gst_object_unref (self->context);
        self->context = NULL;
      }
      self->context =
        gst_gl_display_get_gl_context_for_thread (self->display, NULL);
      if (!self->context) {

        if (!gst_gl_display_create_context (self->display, self->other_context,
              &self->context, &error)) {
          GST_OBJECT_UNLOCK (self->display);
          goto context_error;
        }
      }
    } while (!gst_gl_display_add_context (self->display, self->context));
    GST_OBJECT_UNLOCK (self->display);
  }
  gst_gl_context_thread_add(self->context, (GstGLContextThreadFunc) init_d3d11_context, self);
  // TODO - wait until it happened?


  return TRUE;

context_error:
  {
    if (error) {
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, ("%s", error->message),
          (NULL));
      g_clear_error (&error);
    } else {
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, (NULL), (NULL));
    }
    if (self->context)
      gst_object_unref (self->context);
    self->context = NULL;
    return FALSE;
  }
}

static GstBufferPool *pool = NULL;
static bool shmem_init = false;

static gboolean
gst_shm_sink_propose_allocation (GstBaseSink * sink, GstQuery * query)
{
  GstShmSink *self = GST_SHM_SINK (sink);
  GST_LOG_OBJECT(self, "gst_shm_sink_propose_allocation");

  GstCaps *caps;
  gboolean need_pool;
  gst_query_parse_allocation(query, &caps, &need_pool);
  GstCapsFeatures *features;
  features = gst_caps_get_features (caps, 0);

  if (!gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY)) {
      GST_ERROR_OBJECT(self, "shouldn't GL MEMORY be negotiated?");
  }

  // offer our custom allocator
  GstAllocator *allocator;
  GstAllocationParams params;
  gst_allocation_params_init (&params);

  allocator = GST_ALLOCATOR (self->allocator);
  gst_query_add_allocation_param (query, allocator, &params);
  gst_object_unref (allocator); 

  GstVideoInfo info;
  if (!gst_video_info_from_caps (&info, caps))
    goto invalid_caps;

  if (!shmem_init) {
    //FIXME This should live somewhere else.
    GST_INFO("Initializating shared mem info to %d x %d at %llu fps", info.width, info.height, info.fps_n);
    initialize_shared_memory(self, info.width, info.height, info.fps_n);
    shmem_init = true;
  }

  if (!gst_dshow_filter_sink_ensure_gl_context(self)) {
    return FALSE;
  }
  if (pool) {
    GstStructure *cur_pool_config;
    cur_pool_config = gst_buffer_pool_get_config(pool);
    gint size;
    gst_buffer_pool_config_get_params(cur_pool_config, NULL, &size, NULL, NULL);
    GST_DEBUG("Old pool size: %d New allocation size: info.size: %d", size, info.size);
    if (size == info.size) {
      GST_DEBUG("Reusing buffer pool.");
      gst_query_add_allocation_pool(query, pool, info.size, BUFFER_COUNT, 0);
      return true;
    }
    else {
      GST_DEBUG("The pool buffer size doesn't match (old: %d new: %d). Creating a new one.",
        size, info.size);
      gst_object_unref(pool);
    }
  }
  GST_DEBUG("Make a new buffer pool.");
  pool = gst_gl_buffer_pool_new(self->context);
  GstStructure *config;
  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, info.size, 0, 0);

  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_GL_SYNC_META);
  gst_buffer_pool_config_set_allocator (config, self->allocator, &params);

  if (!gst_buffer_pool_set_config (pool, config)) {
    gst_object_unref (pool);
    goto config_failed;
  }
  /* we need at least 2 buffer because we hold on to the last one */
  gst_query_add_allocation_pool (query, pool, info.size, BUFFER_COUNT, 0);
  GST_DEBUG_OBJECT(self, "Added %" GST_PTR_FORMAT " pool to query", pool);
  return true;

invalid_caps:
  {
    GST_WARNING_OBJECT (self, "invalid caps specified");
    return false;
  }
config_failed:
  {
    GST_WARNING_OBJECT (self, "failed setting config");
    return false;
  }
}
