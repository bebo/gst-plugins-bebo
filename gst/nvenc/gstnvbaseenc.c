/* GStreamer NVENC plugin
 * Copyright (C) 2015 Centricular Ltd
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstnvbaseenc.h"

#include <gst/pbutils/codec-utils.h>

#include <string.h>

#include <GL/glext.h>
#include <GL/wglext.h>
/* #if HAVE_NVENC_GST_GL */
#include <gst/gl/gl.h>
#include <gst/video/gstvideometa.h>
#include "gstdxgidevice.h"

#include <D3d11_4.h>

/* #endif */

/* TODO:
 *  - reset last_flow on FLUSH_STOP (seeking)
 */

/* This currently supports both 5.x and 6.x versions of the NvEncodeAPI.h
 * header which are mostly API compatible. */

#define N_BUFFERS_PER_FRAME 1
#define SUPPORTED_GL_APIS GST_GL_API_OPENGL3
#define BUFFER_COUNT 30

/* magic pointer value we can put in the async queue to signal shut down */
#define SHUTDOWN_COOKIE ((gpointer)GINT_TO_POINTER (1))

#define parent_class gst_nv_base_enc_parent_class
G_DEFINE_ABSTRACT_TYPE (D3DGstNvBaseEnc, gst_nv_base_enc, GST_TYPE_VIDEO_ENCODER);

#define GST_TYPE_NV_PRESET (gst_nv_preset_get_type())

static gboolean gst_nv_base_enc_ensure_gl_context(D3DGstNvBaseEnc * self);
static GType
gst_nv_preset_get_type (void)
{
  static GType nv_preset_type = 0;

  static const GEnumValue presets[] = {
    {GST_NV_PRESET_DEFAULT, "Default", "default"},
    {GST_NV_PRESET_HP, "High Performance", "hp"},
    {GST_NV_PRESET_HQ, "High Quality", "hq"},
/*    {GST_NV_PRESET_BD, "BD", "bd"}, */
    {GST_NV_PRESET_LOW_LATENCY_DEFAULT, "Low Latency", "low-latency"},
    {GST_NV_PRESET_LOW_LATENCY_HQ, "Low Latency, High Quality",
        "low-latency-hq"},
    {GST_NV_PRESET_LOW_LATENCY_HP, "Low Latency, High Performance",
        "low-latency-hp"},
    {GST_NV_PRESET_LOSSLESS_DEFAULT, "Lossless", "lossless"},
    {GST_NV_PRESET_LOSSLESS_HP, "Lossless, High Performance", "lossless-hp"},
    {0, NULL, NULL},
  };

  if (!nv_preset_type) {
    nv_preset_type = g_enum_register_static ("D3DGstNvPreset", presets);
  }
  return nv_preset_type;
}

static GUID
_nv_preset_to_guid (D3DGstNvPreset preset)
{
  GUID null = { 0, };

  switch (preset) {
#define CASE(gst,nv) case G_PASTE(GST_NV_PRESET_,gst): return G_PASTE(G_PASTE(NV_ENC_PRESET_,nv),_GUID)
      CASE (DEFAULT, DEFAULT);
      CASE (HP, HP);
      CASE (HQ, HQ);
/*    CASE (BD, BD);*/
      CASE (LOW_LATENCY_DEFAULT, LOW_LATENCY_DEFAULT);
      CASE (LOW_LATENCY_HQ, LOW_LATENCY_HQ);
      CASE (LOW_LATENCY_HP, LOW_LATENCY_HQ);
      CASE (LOSSLESS_DEFAULT, LOSSLESS_DEFAULT);
      CASE (LOSSLESS_HP, LOSSLESS_HP);
#undef CASE
    default:
      return null;
  }
}

#define GST_TYPE_NV_RC_MODE (gst_nv_rc_mode_get_type())
static GType
gst_nv_rc_mode_get_type (void)
{
  static GType nv_rc_mode_type = 0;

  static const GEnumValue modes[] = {
    {GST_NV_RC_MODE_DEFAULT, "Default (from NVENC preset)", "default"},
    {GST_NV_RC_MODE_CONSTQP, "Constant Quantization", "constqp"},
    {GST_NV_RC_MODE_CBR, "Constant Bit Rate", "cbr"},
    {GST_NV_RC_MODE_VBR, "Variable Bit Rate", "vbr"},
    {GST_NV_RC_MODE_CBR_HQ, "Constant Bit Rate High Quality", "cbrhq" },
    {GST_NV_RC_MODE_VBR_MINQP,
          "Variable Bit Rate (with minimum quantization parameter)",
        "vbr-minqp"},
    {0, NULL, NULL},
  };

  if (!nv_rc_mode_type) {
    nv_rc_mode_type = g_enum_register_static ("D3DGstNvRCMode", modes);
  }
  return nv_rc_mode_type;
}

static NV_ENC_PARAMS_RC_MODE
_rc_mode_to_nv (D3DGstNvRCMode mode)
{
  switch (mode) {
    case GST_NV_RC_MODE_DEFAULT:
      return -1;
#define CASE(gst,nv) case G_PASTE(GST_NV_RC_MODE_,gst): return G_PASTE(NV_ENC_PARAMS_RC_,nv)
      CASE (CONSTQP, CONSTQP);
      CASE (CBR, CBR);
      CASE (VBR, VBR);
      CASE (VBR_MINQP, VBR_MINQP);
      CASE (CBR_HQ, CBR_HQ);
#undef CASE
    default:
      return -1;
  }
}

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        "video/x-raw(memory:GLMemory), "
        "format = (string) RGBA, "
        "width = (int) [ 16, 4096 ], height = (int) [ 16, 2160 ], "
        "framerate = (fraction) [0, MAX]"
    ));
// PROP DEFINITION
enum
{
  PROP_0,
  PROP_DEVICE_ID,
  PROP_PRESET,
  PROP_BITRATE,
  PROP_RC_MODE,
  PROP_QP_MIN,
  PROP_QP_MAX,
  PROP_QP_CONST,  
  PROP_GOP_SIZE,
  PROP_FPS,
  PROP_LAST_FRAME_NUMBER_FPS,
  PROP_LAST_FRAME_TIME_FPS
};

#define DEFAULT_PRESET GST_NV_PRESET_DEFAULT
#define DEFAULT_BITRATE 0
#define DEFAULT_FPS 0
#define DEFAULT_LAST_FRAME_FPS 0
#define DEFAULT_TIME_LAST_FRAME_FPS 0
#define DEFAULT_RC_MODE GST_NV_RC_MODE_DEFAULT
#define DEFAULT_QP_MIN -1
#define DEFAULT_QP_MAX -1
#define DEFAULT_QP_CONST -1
#define DEFAULT_GOP_SIZE 75

/* This lock is needed to prevent the situation where multiple encoders are
 * initialised at the same time which appears to cause excessive CPU usage over
 * some period of time. */
G_LOCK_DEFINE_STATIC (initialization_lock);

#if HAVE_NVENC_GST_GL
struct gl_input_resource
{
  GstBuffer * buf;
  GstGLMemory *gl_mem[GST_VIDEO_MAX_PLANES];
  struct cudaGraphicsResource *cuda_texture;
  gpointer cuda_plane_pointers[GST_VIDEO_MAX_PLANES];
  gpointer cuda_pointer;
  gsize cuda_stride;
  gsize cuda_num_bytes;

  NV_ENC_REGISTER_RESOURCE nv_resource;
  NV_ENC_MAP_INPUT_RESOURCE nv_mapped_resource;
};
#endif

struct frame_state
{
  gint n_buffers;
  gpointer in_bufs[N_BUFFERS_PER_FRAME];
  gpointer out_bufs[N_BUFFERS_PER_FRAME];
};

static gboolean gst_nv_base_enc_open (GstVideoEncoder * enc);
static gboolean gst_nv_base_enc_close (GstVideoEncoder * enc);
static gboolean gst_nv_base_enc_start (GstVideoEncoder * enc);
static gboolean gst_nv_base_enc_stop (GstVideoEncoder * enc);
static void gst_nv_base_enc_set_context (GstElement * element,
    GstContext * context);
static gboolean gst_nv_base_enc_sink_query (GstVideoEncoder * enc,
    GstQuery * query);
static gboolean gst_nv_base_enc_set_format (GstVideoEncoder * enc,
    GstVideoCodecState * state);
static GstFlowReturn gst_nv_base_enc_handle_frame (GstVideoEncoder * enc,
    GstVideoCodecFrame * frame);
static void gst_nv_base_enc_free_buffers (D3DGstNvBaseEnc * nvenc);
static GstFlowReturn gst_nv_base_enc_finish (GstVideoEncoder * enc);
static void gst_nv_base_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_nv_base_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_nv_base_enc_finalize (GObject * obj);
static GstCaps *gst_nv_base_enc_getcaps (GstVideoEncoder * enc,
    GstCaps * filter);
static gboolean gst_nv_base_enc_stop_bitstream_thread (D3DGstNvBaseEnc * nvenc);
static gboolean gst_nv_base_enc_propose_allocation (GstVideoEncoder * enc, GstQuery * query);

static void
gst_nv_base_enc_class_init (D3DGstNvBaseEncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *videoenc_class = GST_VIDEO_ENCODER_CLASS (klass);
  // Why does GStreamer load the dll and do nothing with it?  This seems like a bug. 
  // We need to load it where we initialize the encoder.
  //void *nvenc = LoadLibrary("nvEncodeAPI64.dll");
  //if (nvenc == NULL) {
  //    GST_ERROR("Failed to load nvEncodeAPI64.dll");
  //}
  gobject_class->set_property = gst_nv_base_enc_set_property;
  gobject_class->get_property = gst_nv_base_enc_get_property;
  gobject_class->finalize = gst_nv_base_enc_finalize;

  element_class->set_context = GST_DEBUG_FUNCPTR (gst_nv_base_enc_set_context);

  videoenc_class->open = GST_DEBUG_FUNCPTR (gst_nv_base_enc_open);
  videoenc_class->close = GST_DEBUG_FUNCPTR (gst_nv_base_enc_close);

  videoenc_class->start = GST_DEBUG_FUNCPTR (gst_nv_base_enc_start);
  videoenc_class->stop = GST_DEBUG_FUNCPTR (gst_nv_base_enc_stop);
  videoenc_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_nv_base_enc_propose_allocation);

  videoenc_class->set_format = GST_DEBUG_FUNCPTR (gst_nv_base_enc_set_format);
  videoenc_class->getcaps = GST_DEBUG_FUNCPTR (gst_nv_base_enc_getcaps);
  videoenc_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_nv_base_enc_handle_frame);
  videoenc_class->finish = GST_DEBUG_FUNCPTR (gst_nv_base_enc_finish);
  videoenc_class->sink_query = GST_DEBUG_FUNCPTR (gst_nv_base_enc_sink_query);

  gst_element_class_add_static_pad_template (element_class, &sink_factory);

  g_object_class_install_property (gobject_class, PROP_DEVICE_ID,
      g_param_spec_uint ("cuda-device-id",
          "Cuda Device ID",
          "Set the GPU device to use for operations",
          0, G_MAXUINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PRESET,
      g_param_spec_enum ("preset", "Encoding Preset",
          "Encoding Preset",
          GST_TYPE_NV_PRESET, DEFAULT_PRESET,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_RC_MODE,
      g_param_spec_enum ("rc-mode", "RC Mode", "Rate Control Mode",
          GST_TYPE_NV_RC_MODE, DEFAULT_RC_MODE,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_QP_MIN,
      g_param_spec_int ("qp-min", "Minimum Quantizer",
          "Minimum quantizer (-1 = from NVENC preset)", -1, 51, DEFAULT_QP_MIN,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_QP_MAX,
      g_param_spec_int ("qp-max", "Maximum Quantizer",
          "Maximum quantizer (-1 = from NVENC preset)", -1, 51, DEFAULT_QP_MAX,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_QP_CONST,
      g_param_spec_int ("qp-const", "Constant Quantizer",
          "Constant quantizer (-1 = from NVENC preset)", -1, 51,
          DEFAULT_QP_CONST,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_GOP_SIZE,
      g_param_spec_int ("gop-size", "GOP size",
          "Number of frames between intra frames (-1 = infinite)",
          -1, G_MAXINT, DEFAULT_GOP_SIZE,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
              G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate",
          "Bitrate in kbit/sec (0 = from NVENC preset)", 0, 2000 * 1024,
          DEFAULT_BITRATE,
          G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_STATIC_STRINGS));
  g_object_class_install_property(gobject_class, PROP_FPS,
    g_param_spec_int("fps", "FPS",
      "Number of frames in a sec", 0, 1000,
      DEFAULT_FPS,
      G_PARAM_READWRITE | GST_PARAM_MUTABLE_PLAYING |
      G_PARAM_STATIC_STRINGS));
}

static gboolean
_get_supported_input_formats (D3DGstNvBaseEnc * nvenc)
{
  D3DGstNvBaseEncClass *nvenc_class = GST_D3D_NV_BASE_ENC_GET_CLASS (nvenc);
  guint64 format_mask = 0;
  uint32_t i, num = 0;
  NV_ENC_BUFFER_FORMAT formats[64];
  GValue list = G_VALUE_INIT;
  GValue val = G_VALUE_INIT;

  NvEncGetInputFormats (nvenc->encoder, nvenc_class->codec_id, formats,
      G_N_ELEMENTS (formats), &num);

  for (i = 0; i < num; ++i) {
    GST_INFO_OBJECT (nvenc, "input format: 0x%08x", formats[i]);
    /* Apparently we can just ignore the tiled formats and can feed
     * it the respective untiled planar format instead ?! */
    switch (formats[i]) {
      case NV_ENC_BUFFER_FORMAT_NV12_PL:
#if defined (NV_ENC_BUFFER_FORMAT_NV12_TILED16x16)
      case NV_ENC_BUFFER_FORMAT_NV12_TILED16x16:
#endif
#if defined (NV_ENC_BUFFER_FORMAT_NV12_TILED64x16)
      case NV_ENC_BUFFER_FORMAT_NV12_TILED64x16:
#endif
        format_mask |= (1 << GST_VIDEO_FORMAT_NV12);
        break;
      case NV_ENC_BUFFER_FORMAT_YV12_PL:
#if defined(NV_ENC_BUFFER_FORMAT_YV12_TILED16x16)
      case NV_ENC_BUFFER_FORMAT_YV12_TILED16x16:
#endif
#if defined (NV_ENC_BUFFER_FORMAT_YV12_TILED64x16)
      case NV_ENC_BUFFER_FORMAT_YV12_TILED64x16:
#endif
        format_mask |= (1 << GST_VIDEO_FORMAT_YV12);
        break;
      case NV_ENC_BUFFER_FORMAT_IYUV_PL:
#if defined (NV_ENC_BUFFER_FORMAT_IYUV_TILED16x16)
      case NV_ENC_BUFFER_FORMAT_IYUV_TILED16x16:
#endif
#if defined (NV_ENC_BUFFER_FORMAT_IYUV_TILED64x16)
      case NV_ENC_BUFFER_FORMAT_IYUV_TILED64x16:
#endif
        format_mask |= (1 << GST_VIDEO_FORMAT_I420);
        break;
      case NV_ENC_BUFFER_FORMAT_YUV444_PL:
#if defined (NV_ENC_BUFFER_FORMAT_YUV444_TILED16x16)
      case NV_ENC_BUFFER_FORMAT_YUV444_TILED16x16:
#endif
#if defined (NV_ENC_BUFFER_FORMAT_YUV444_TILED64x16)
      case NV_ENC_BUFFER_FORMAT_YUV444_TILED64x16:
#endif
      {
        NV_ENC_CAPS_PARAM caps_param = { 0, };
        int yuv444_supported = 0;

        caps_param.version = NV_ENC_CAPS_PARAM_VER;
        caps_param.capsToQuery = NV_ENC_CAPS_SUPPORT_YUV444_ENCODE;

        if (NvEncGetEncodeCaps (nvenc->encoder, nvenc_class->codec_id,
                &caps_param, &yuv444_supported) != NV_ENC_SUCCESS)
          yuv444_supported = 0;

        if (yuv444_supported)
          format_mask |= (1 << GST_VIDEO_FORMAT_Y444);
        break;
      }
      default:
        GST_FIXME ("unmapped input format: 0x%08x", formats[i]);
        break;
    }
  }

  if (format_mask == 0)
    return FALSE;

  /* process a second time so we can add formats in the order we want */
  g_value_init (&list, GST_TYPE_LIST);
  g_value_init (&val, G_TYPE_STRING);
  if ((format_mask & (1 << GST_VIDEO_FORMAT_NV12))) {
    g_value_set_static_string (&val, "NV12");
    gst_value_list_append_value (&list, &val);
  }
  if ((format_mask & (1 << GST_VIDEO_FORMAT_YV12))) {
    g_value_set_static_string (&val, "YV12");
    gst_value_list_append_value (&list, &val);
  }
  if ((format_mask & (1 << GST_VIDEO_FORMAT_I420))) {
    g_value_set_static_string (&val, "I420");
    gst_value_list_append_value (&list, &val);
  }
  if ((format_mask & (1 << GST_VIDEO_FORMAT_Y444))) {
    g_value_set_static_string (&val, "Y444");
    gst_value_list_append_value (&list, &val);
  }
  g_value_unset (&val);

  GST_OBJECT_LOCK (nvenc);
  g_free (nvenc->input_formats);
  nvenc->input_formats = g_memdup (&list, sizeof (GValue));
  GST_OBJECT_UNLOCK (nvenc);

  return TRUE;
}


struct NvEncOpenEncodeSessionExParams {
  NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS * params;
  void ** encoder;
  NVENCSTATUS status;
};

static void 
gl_run_NvEncOpenEncodeSessionEx(GstGLContext *context, struct NvEncOpenEncodeSessionExParams * p) {
  p->status = NvEncOpenEncodeSessionEx (p->params, p->encoder);
}

static int gl_NvEncOpenEncodeSessionEx(GstGLContext *context, NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS* params, void ** encoder) {

  struct NvEncOpenEncodeSessionExParams p = {
      .params = params,
      .encoder = encoder,
      .status = 0
  };
  gst_gl_context_thread_add(context, (GstGLContextThreadFunc)gl_run_NvEncOpenEncodeSessionEx, &p);
  return p.status;
}

//NVENCSTATUS NVENCAPI NvEncInitializeEncoder  (void* encoder, NV_ENC_INITIALIZE_PARAMS* createEncodeParams);
struct NvEncInitializeEncoderParams {
  void * encoder;
  NV_ENC_INITIALIZE_PARAMS * createEncodeParams;
  NVENCSTATUS status;
};

static void 
gl_run_NvEncInitializeEncoder(GstGLContext *context, struct NvEncInitializeEncoderParams * p) {
  p->status = NvEncInitializeEncoder(p->encoder, p->createEncodeParams);
}

static int gl_NvEncInitializeEncoder(GstGLContext *context,void* encoder, NV_ENC_INITIALIZE_PARAMS* createEncodeParams)
{

  struct NvEncInitializeEncoderParams p = {
      .encoder = encoder,
      .createEncodeParams = createEncodeParams,
      .status = 0
  };
  gst_gl_context_thread_add(context, (GstGLContextThreadFunc)gl_run_NvEncInitializeEncoder, &p);
  return p.status;
}

// NVENCSTATUS NVENCAPI NvEncReconfigureEncoder                   (void *encoder, NV_ENC_RECONFIGURE_PARAMS* reInitEncodeParams);
struct NvEncReconfigureEncoderParams {
  void * encoder;
  NV_ENC_RECONFIGURE_PARAMS * reInitEncodeParams;
  NVENCSTATUS status;
};

static void 
gl_run_NvEncReconfigureEncoder(GstGLContext *context, struct NvEncReconfigureEncoderParams * p) {
  p->status = NvEncReconfigureEncoder(p->encoder, p->reInitEncodeParams);
}

static int gl_NvEncReconfigureEncoder(GstGLContext *context,void* encoder, NV_ENC_RECONFIGURE_PARAMS* reInitEncodeParams)
{

  struct NvEncReconfigureEncoderParams p = {
      .encoder = encoder,
      .reInitEncodeParams = reInitEncodeParams,
      .status = 0
  };
  gst_gl_context_thread_add(context, (GstGLContextThreadFunc)gl_run_NvEncReconfigureEncoder, &p);
  return p.status;
}

// NVENCSTATUS NVENCAPI NvEncCreateInputBuffer                     (void* encoder, NV_ENC_CREATE_INPUT_BUFFER* createInputBufferParams);
struct NvEncCreateInputBufferParams {
  void * encoder;
  NV_ENC_CREATE_INPUT_BUFFER * createInputBufferParams;
  NVENCSTATUS status;
};

static void 
gl_run_NvEncCreateInputBuffer(GstGLContext *context, struct NvEncCreateInputBufferParams * p) {
  p->status = NvEncCreateInputBuffer(p->encoder, p->createInputBufferParams);
}

static int gl_NvEncCreateInputBuffer(GstGLContext *context,void* encoder, NV_ENC_CREATE_INPUT_BUFFER* createInputBufferParams)
{

  struct NvEncCreateInputBufferParams p = {
      .encoder = encoder,
      .createInputBufferParams = createInputBufferParams,
      .status = 0
  };
  gst_gl_context_thread_add(context, (GstGLContextThreadFunc)gl_run_NvEncCreateInputBuffer, &p);
  return p.status;
}

// NVENCSTATUS NVENCAPI NvEncCreateBitstreamBuffer                 (void* encoder, NV_ENC_CREATE_BITSTREAM_BUFFER* createBitstreamBufferParams);
struct NvEncCreateBitstreamBufferParams {
  void * encoder;
  NV_ENC_CREATE_BITSTREAM_BUFFER * createBitstreamBufferParams;
  NVENCSTATUS status;
};

static void 
gl_run_NvEncCreateBitstreamBuffer(GstGLContext *context, struct NvEncCreateBitstreamBufferParams * p) {
  p->status = NvEncCreateBitstreamBuffer(p->encoder, p->createBitstreamBufferParams);
}

static int gl_NvEncCreateBitstreamBuffer(GstGLContext *context,void* encoder, NV_ENC_CREATE_BITSTREAM_BUFFER* createBitstreamBufferParams)
{

  struct NvEncCreateBitstreamBufferParams p = {
      .encoder = encoder,
      .createBitstreamBufferParams = createBitstreamBufferParams,
      .status = 0
  };
  gst_gl_context_thread_add(context, (GstGLContextThreadFunc)gl_run_NvEncCreateBitstreamBuffer, &p);
  return p.status;
}

// NVENCSTATUS NVENCAPI NvEncEncodePicture                         (void* encoder, NV_ENC_PIC_PARAMS* encodePicParams);
struct NvEncEncodePictureParams {
  void * encoder;
  NV_ENC_PIC_PARAMS * encodePicParams;
  NVENCSTATUS status;
};

static void 
gl_run_NvEncEncodePicture(GstGLContext *context, struct NvEncEncodePictureParams * p) {
  p->status = NvEncEncodePicture(p->encoder, p->encodePicParams);
}

static int gl_NvEncEncodePicture(GstGLContext *context,void* encoder, NV_ENC_PIC_PARAMS* encodePicParams)
{

  struct NvEncEncodePictureParams p = {
      .encoder = encoder,
      .encodePicParams = encodePicParams,
      .status = 0
  };
  gst_gl_context_thread_add(context, (GstGLContextThreadFunc)gl_run_NvEncEncodePicture, &p);
  return p.status;
}

static gboolean
gst_nv_base_enc_open (GstVideoEncoder * enc)
{
  GST_INFO("OPEN NVENC");
  D3DGstNvBaseEnc *nvenc = GST_D3D_NV_BASE_ENC (enc);
  if (!gst_nv_base_enc_ensure_gl_context(nvenc)) {
    GST_ERROR("COULD NOT OPEN");
    return FALSE;
  }

  {
    GST_INFO("CREATING_ENCODER");
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS params = { 0, };
    NVENCSTATUS nv_ret;
    GstDXGID3D11Context *share_context = get_dxgi_share_context(nvenc->context);
    params.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    params.apiVersion = NVENCAPI_VERSION;
    params.device = share_context->d3d11_device;
    params.deviceType = NV_ENC_DEVICE_TYPE_DIRECTX;
    if (!share_context) {
      GST_ERROR("No DXGI share context.");
      return FALSE;
    }
    else if (!nvenc->context) {
      GST_ERROR("No GL context.");
      return FALSE;
    }
    else if (!params.device) {
      GST_ERROR("No d3d11_device");
      return FALSE;
    }

    nv_ret = gl_NvEncOpenEncodeSessionEx(nvenc->context, &params, &nvenc->encoder);
    if (nv_ret != NV_ENC_SUCCESS) {
      GST_ERROR ("Failed to create NVENC encoder session, ret=%d", nv_ret);
      return FALSE;
    }
    GST_INFO ("created NVENC encoder %p", nvenc->encoder);
  }

  /* query supported input formats */
  if (!_get_supported_input_formats (nvenc)) {
    GST_WARNING_OBJECT (nvenc, "No supported input formats");
    gst_nv_base_enc_close (enc);
    return FALSE;
  }

  return TRUE;
}

static void
gst_nv_base_enc_set_context (GstElement * element, GstContext * context)
{
  D3DGstNvBaseEnc *nvenc = GST_D3D_NV_BASE_ENC (element);

#if HAVE_NVENC_GST_GL
  gst_gl_handle_set_context (element, context,
      (GstGLDisplay **) & nvenc->display,
      (GstGLContext **) & nvenc->other_context);
  if (nvenc->display)
    gst_gl_display_filter_gl_api (GST_GL_DISPLAY (nvenc->display),
        SUPPORTED_GL_APIS);
#endif

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_nv_base_enc_sink_query (GstVideoEncoder * enc, GstQuery * query)
{
  D3DGstNvBaseEnc *nvenc = GST_D3D_NV_BASE_ENC (enc);

  switch (GST_QUERY_TYPE (query)) {
#if HAVE_NVENC_GST_GL
    case GST_QUERY_CONTEXT:{
      gboolean ret;

      ret = gst_gl_handle_context_query ((GstElement *) nvenc, query,
          nvenc->display, NULL, nvenc->other_context);
      if (nvenc->display)
        gst_gl_display_filter_gl_api (GST_GL_DISPLAY (nvenc->display),
            SUPPORTED_GL_APIS);

      if (ret)
        return ret;
      break;
    }
#endif
    default:
      break;
  }

  return GST_VIDEO_ENCODER_CLASS (parent_class)->sink_query (enc, query);
}

static gboolean
gst_nv_base_enc_start (GstVideoEncoder * enc)
{
  D3DGstNvBaseEnc *nvenc = GST_D3D_NV_BASE_ENC (enc);

  nvenc->bitstream_pool = g_async_queue_new ();
  nvenc->bitstream_queue = g_async_queue_new ();
  nvenc->in_bufs_pool = g_async_queue_new ();
  nvenc->holding_queue = g_async_queue_new(); 

  nvenc->last_flow = GST_FLOW_OK;
  nvenc->allocator = gst_gl_dxgi_memory_allocator_new();

/* #if HAVE_NVENC_GST_GL */
/*   { */
/*     gst_gl_ensure_element_data (GST_ELEMENT (nvenc), */
/*         (GstGLDisplay **) & nvenc->display, */
/*         (GstGLContext **) & nvenc->other_context); */
/*     if (nvenc->display) */
/*       gst_gl_display_filter_gl_api (GST_GL_DISPLAY (nvenc->display), */
/*           SUPPORTED_GL_APIS); */
/*   } */
/* #endif */

  return TRUE;
}

static gboolean
gst_nv_base_enc_ensure_gl_context(D3DGstNvBaseEnc * self)
{
  return gst_dxgi_device_ensure_gl_context((GstElement *) self,
    (GstGLContext **) &self->context,
    (GstGLContext **) &self->other_context,
    (GstGLDisplay **) &self->display);
}

static gboolean
gst_nv_base_enc_stop (GstVideoEncoder * enc)
{
  D3DGstNvBaseEnc *nvenc = GST_D3D_NV_BASE_ENC (enc);
  GST_INFO("Stopping NVENC");
  gst_nv_base_enc_drain_encoder(nvenc);
  gst_nv_base_enc_stop_bitstream_thread (nvenc);
  gst_nv_base_enc_free_buffers (nvenc);

  if (nvenc->bitstream_pool) {
    g_async_queue_unref (nvenc->bitstream_pool);
    nvenc->bitstream_pool = NULL;
  }
  if (nvenc->holding_queue) {
    g_async_queue_unref(nvenc->holding_queue);
    nvenc->holding_queue = NULL;
  }
  if (nvenc->bitstream_queue) {
    g_async_queue_unref (nvenc->bitstream_queue);
    nvenc->bitstream_queue = NULL;
  }
  if (nvenc->in_bufs_pool) {
    g_async_queue_unref (nvenc->in_bufs_pool);
    nvenc->in_bufs_pool = NULL;
  }
  if (nvenc->display) {
    gst_object_unref (nvenc->display);
    nvenc->display = NULL;
  }
  if (nvenc->other_context) {
    gst_object_unref (nvenc->other_context);
    nvenc->other_context = NULL;
  }
  if (nvenc->pool) {
    gst_object_unref (nvenc->pool);
  }
  nvenc->pool = NULL;

  if (nvenc->allocator) {
    gst_object_unref (nvenc->allocator);
  }
  nvenc->allocator = NULL;
  GST_INFO("Stop complete");
  return TRUE;
}

static GValue *
_get_interlace_modes (D3DGstNvBaseEnc * nvenc)
{
  D3DGstNvBaseEncClass *nvenc_class = GST_D3D_NV_BASE_ENC_GET_CLASS (nvenc);
  NV_ENC_CAPS_PARAM caps_param = { 0, };
  GValue *list = g_new0 (GValue, 1);
  GValue val = G_VALUE_INIT;

  g_value_init (list, GST_TYPE_LIST);
  g_value_init (&val, G_TYPE_STRING);

  g_value_set_static_string (&val, "progressive");
  gst_value_list_append_value (list, &val);

  caps_param.version = NV_ENC_CAPS_PARAM_VER;
  caps_param.capsToQuery = NV_ENC_CAPS_SUPPORT_FIELD_ENCODING;

  if (NvEncGetEncodeCaps (nvenc->encoder, nvenc_class->codec_id,
          &caps_param, &nvenc->interlace_modes) != NV_ENC_SUCCESS)
    nvenc->interlace_modes = 0;

  if (nvenc->interlace_modes >= 1) {
    g_value_set_static_string (&val, "interleaved");
    gst_value_list_append_value (list, &val);
    g_value_set_static_string (&val, "mixed");
    gst_value_list_append_value (list, &val);
  }
  /* TODO: figure out what nvenc frame based interlacing means in gst terms */

  return list;
}

static GstCaps *
gst_nv_base_enc_getcaps (GstVideoEncoder * enc, GstCaps * filter)
{
  D3DGstNvBaseEnc *nvenc = GST_D3D_NV_BASE_ENC (enc);
  GstCaps *supported_incaps = NULL;
  GstCaps *template_caps, *caps;

  GST_OBJECT_LOCK (nvenc);

  if (nvenc->input_formats != NULL) {
    GValue *val;

    template_caps = gst_pad_get_pad_template_caps (enc->sinkpad);
    supported_incaps = gst_caps_copy (template_caps);
    gst_caps_set_value (supported_incaps, "format", nvenc->input_formats);

    val = _get_interlace_modes (nvenc);
    gst_caps_set_value (supported_incaps, "interlace-mode", val);
    g_free (val);

    GST_LOG_OBJECT (enc, "codec input caps %" GST_PTR_FORMAT, supported_incaps);
    GST_LOG_OBJECT (enc, "   template caps %" GST_PTR_FORMAT, template_caps);
    caps = gst_caps_intersect (template_caps, supported_incaps);
    gst_caps_unref (template_caps);
    gst_caps_unref (supported_incaps);
    supported_incaps = caps;
    GST_LOG_OBJECT (enc, "  supported caps %" GST_PTR_FORMAT, supported_incaps);
  }

  GST_OBJECT_UNLOCK (nvenc);

  caps = gst_video_encoder_proxy_getcaps (enc, supported_incaps, filter);

  if (supported_incaps)
    gst_caps_unref (supported_incaps);

  GST_DEBUG_OBJECT (nvenc, "  returning caps %" GST_PTR_FORMAT, caps);

  return caps;
}

static gboolean
gst_nv_base_enc_close (GstVideoEncoder * enc)
{
  D3DGstNvBaseEnc *nvenc = GST_D3D_NV_BASE_ENC (enc);
  GST_DEBUG("Closing encoder");
  if (nvenc->encoder) {
    if (NvEncDestroyEncoder (nvenc->encoder) != NV_ENC_SUCCESS)
      return FALSE;
    nvenc->encoder = NULL;
  }

  GST_OBJECT_LOCK (nvenc);
  g_free (nvenc->input_formats);
  nvenc->input_formats = NULL;
  GST_OBJECT_UNLOCK (nvenc);

  if (nvenc->input_state) {
    gst_video_codec_state_unref (nvenc->input_state);
    nvenc->input_state = NULL;
  }

  if (nvenc->bitstream_pool != NULL) {
    g_assert (g_async_queue_length (nvenc->bitstream_pool) == 0);
    g_async_queue_unref (nvenc->bitstream_pool);
    nvenc->bitstream_pool = NULL;
  }
  GST_DEBUG("Encoder successfully closed");
  return TRUE;
}

static void
gst_nv_base_enc_init (D3DGstNvBaseEnc * nvenc)
{
  GstVideoEncoder *encoder = GST_VIDEO_ENCODER (nvenc);

  nvenc->preset_enum = DEFAULT_PRESET;
  nvenc->selected_preset = _nv_preset_to_guid (nvenc->preset_enum);
  nvenc->rate_control_mode = DEFAULT_RC_MODE;
  nvenc->qp_min = DEFAULT_QP_MIN;
  nvenc->qp_max = DEFAULT_QP_MAX;
  nvenc->qp_const = DEFAULT_QP_CONST;
  nvenc->bitrate = DEFAULT_BITRATE;
  nvenc->gop_size = DEFAULT_GOP_SIZE;

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
}

static void
gst_nv_base_enc_finalize (GObject * obj)
{
  G_OBJECT_CLASS (gst_nv_base_enc_parent_class)->finalize (obj);
}

static GstVideoCodecFrame *
_find_frame_with_output_buffer (D3DGstNvBaseEnc * nvenc, NV_ENC_OUTPUT_PTR out_buf)
{
  GList *l, *walk = gst_video_encoder_get_frames (GST_VIDEO_ENCODER (nvenc));
  GstVideoCodecFrame *ret = NULL;
  gint i;

  for (l = walk; l; l = l->next) {
    GstVideoCodecFrame *frame = (GstVideoCodecFrame *) l->data;
    struct frame_state *state = frame->user_data;

    if (!state)
      continue;

    for (i = 0; i < N_BUFFERS_PER_FRAME; i++) {

      if (!state->out_bufs[i])
        break;

      if (state->out_bufs[i] == out_buf)
        ret = frame;
    }
  }

  if (ret)
    gst_video_codec_frame_ref (ret);

  g_list_free_full (walk, (GDestroyNotify) gst_video_codec_frame_unref);

  return ret;
}

struct bslock {
  NV_ENC_LOCK_BITSTREAM *lock_bs;
  void* encoder;
  NV_ENC_OUTPUT_PTR out_buf;
};

static void
lock_bitstream_helper(GstGLContext *ctx, struct bslock* bs) {
  NVENCSTATUS nv_ret = NvEncLockBitstream(bs->encoder, bs->lock_bs);
  if (nv_ret != NV_ENC_SUCCESS) {
    /* FIXME: what to do here? */
    GST_ERROR("Failed to lock bitstream: %d, %d", nv_ret, bs->out_buf);
    bs->out_buf = SHUTDOWN_COOKIE;
  }
  else {
    GST_INFO("LOCKED Bitstream: %d", bs->out_buf);
  }
}

static void
unlock_bitstream_helper(GstGLContext *ctx, struct bslock* bs) {
  NVENCSTATUS nv_ret = NvEncUnlockBitstream(bs->encoder, bs->out_buf);
  if (nv_ret != NV_ENC_SUCCESS) {
    /* FIXME: what to do here? */
    GST_ERROR("Failed to unlock bitsream: %d", nv_ret);
    bs->out_buf = SHUTDOWN_COOKIE;
  }
}

struct umh {
  void* encoder;
  struct gl_input_resource * in_gl_resource;
};

static void
unmap_helper(GstGLContext *ctx, struct umh *u) {
  NVENCSTATUS nv_ret =
    NvEncUnmapInputResource(u->encoder,
      u->in_gl_resource->nv_mapped_resource.mappedResource);
  if (nv_ret != NV_ENC_SUCCESS) {
    GST_ERROR("Failed to unmap input resource %" GST_PTR_FORMAT ", ret %d",
      u->in_gl_resource, nv_ret);
  }
  nv_ret =
    NvEncUnregisterResource(u->encoder,
      u->in_gl_resource->nv_resource.registeredResource);
  if (nv_ret != NV_ENC_SUCCESS)
    GST_ERROR("Failed to unregister resource %" GST_PTR_FORMAT ", ret %d",
      u->in_gl_resource, nv_ret);
}

static gpointer
gst_nv_base_enc_bitstream_thread (gpointer user_data)
{
  GstVideoEncoder *enc = user_data;
  D3DGstNvBaseEnc *nvenc = user_data;
  GstDXGID3D11Context * ctx = get_dxgi_share_context(nvenc->context);

  /* overview of operation:
   * 1. retreive the next buffer submitted to the bitstream pool
   * 2. wait for that buffer to be ready from nvenc (LockBitsream)
   * 3. retreive the GstVideoCodecFrame associated with that buffer
   * 4. for each buffer in the frame
   * 4.1 (step 2): wait for that buffer to be ready from nvenc (LockBitsream)
   * 4.2 create an output GstBuffer from the nvenc buffers
   * 4.3 unlock the nvenc bitstream buffers UnlockBitsream
   * 5. finish_frame()
   * 6. cleanup
   */
  do {
    GstBuffer *buffers[N_BUFFERS_PER_FRAME];
    struct frame_state *state = NULL;
    GstVideoCodecFrame *frame = NULL;
    GstFlowReturn flow = GST_FLOW_OK;
    gint i;

    {
      NV_ENC_LOCK_BITSTREAM lock_bs = { 0, };
      NV_ENC_OUTPUT_PTR out_buf;

      for (i = 0; i < N_BUFFERS_PER_FRAME; i++) {
        /* get and lock bitstream buffers */
        GstVideoCodecFrame *tmp_frame;

        if (state && i >= state->n_buffers)
          break;

        GST_LOG_OBJECT (enc, "wait for bitstream buffer..");

        /* assumes buffers are submitted in order */
        out_buf = g_async_queue_pop (nvenc->bitstream_queue);
        if ((gpointer)out_buf == SHUTDOWN_COOKIE) {
          GST_DEBUG("Bitstream thread got shutdown cookie");
          break;
        }


        GST_LOG_OBJECT (nvenc, "waiting for output buffer %p to be ready",
            out_buf);

        lock_bs.version = NV_ENC_LOCK_BITSTREAM_VER;
        lock_bs.outputBitstream = out_buf;
        lock_bs.doNotWait = 0;

        /* FIXME: this would need to be updated for other slice modes */
        lock_bs.sliceOffsets = NULL;
        struct bslock b = {
          .lock_bs = &lock_bs,
          .out_buf = out_buf,
          .encoder = nvenc->encoder
        };
        lock_bitstream_helper(nvenc->context, &b);

        GST_LOG_OBJECT (nvenc, "picture type %d", lock_bs.pictureType);

        tmp_frame = _find_frame_with_output_buffer (nvenc, out_buf);
        g_assert (tmp_frame != NULL);
        if (frame)
          g_assert (frame == tmp_frame);
        frame = tmp_frame;

        state = frame->user_data;
        g_assert (state->out_bufs[i] == out_buf);

        /* copy into output buffer */
        buffers[i] =
            gst_buffer_new_allocate (NULL, lock_bs.bitstreamSizeInBytes, NULL);
        gst_buffer_fill (buffers[i], 0, lock_bs.bitstreamBufferPtr,
            lock_bs.bitstreamSizeInBytes);

        if (lock_bs.pictureType == NV_ENC_PIC_TYPE_IDR) {
          GST_DEBUG_OBJECT (nvenc, "This is a keyframe");
          GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
        }

        /* TODO: use lock_bs.outputTimeStamp and lock_bs.outputDuration */
        /* TODO: check pts/dts is handled properly if there are B-frames */

        unlock_bitstream_helper(nvenc->context, &b);

        GST_LOG_OBJECT (nvenc, "returning bitstream buffer %p to pool",
            state->out_bufs[i]);
        g_async_queue_push (nvenc->bitstream_pool, state->out_bufs[i]);
      }

      if (out_buf == SHUTDOWN_COOKIE)
        break;
    }

    {
      GstBuffer *output_buffer = gst_buffer_new ();

      for (i = 0; i < state->n_buffers; i++)
        output_buffer = gst_buffer_append (output_buffer, buffers[i]);

      frame->output_buffer = output_buffer;
    }

    for (i = 0; i < state->n_buffers; i++) {
      void *in_buf = state->in_bufs[i];
      g_assert (in_buf != NULL);
      if (nvenc->gl_input) {
        struct gl_input_resource *in_gl_resource = in_buf;

        struct umh u = {
          .encoder = nvenc->encoder,
          .in_gl_resource = in_gl_resource
        };
        gst_gl_context_thread_add(nvenc->context, unmap_helper, &u);

        memset (&in_gl_resource->nv_mapped_resource, 0,
            sizeof (in_gl_resource->nv_mapped_resource));
        gst_buffer_unref(in_gl_resource->buf);
        in_gl_resource->buf = NULL;
      }


      g_async_queue_push (nvenc->in_bufs_pool, in_buf);
    }

    flow = gst_video_encoder_finish_frame (enc, frame);
    frame = NULL;

    if (flow != GST_FLOW_OK) {
      GST_INFO_OBJECT (enc, "got flow %s", gst_flow_get_name (flow));
      if (flow != GST_FLOW_FLUSHING || (
        !g_async_queue_length(nvenc->bitstream_queue) &&
        !g_async_queue_length(nvenc->holding_queue))) {
        g_atomic_int_set(&nvenc->last_flow, flow);
        break;
      }
    }
  }
  while (TRUE);

  GST_INFO_OBJECT (nvenc, "exiting thread");

  return NULL;
}

static gboolean
gst_nv_base_enc_start_bitstream_thread (D3DGstNvBaseEnc * nvenc)
{
  gchar *name = g_strdup_printf ("%s-read-bits", GST_OBJECT_NAME (nvenc));

  g_assert (nvenc->bitstream_thread == NULL);

  g_assert (g_async_queue_length (nvenc->bitstream_queue) == 0);

  nvenc->bitstream_thread =
      g_thread_try_new (name, gst_nv_base_enc_bitstream_thread, nvenc, NULL);

  g_free (name);

  if (nvenc->bitstream_thread == NULL)
    return FALSE;

  GST_INFO_OBJECT (nvenc, "started thread to read bitstream");
  return TRUE;
}

static gboolean
gst_nv_base_enc_stop_bitstream_thread (D3DGstNvBaseEnc * nvenc)
{
  gpointer out_buf;
  GST_DEBUG("Stopping bitstream thread.");
  if (nvenc->bitstream_thread == NULL)
    return TRUE;
  g_async_queue_lock(nvenc->bitstream_queue);
  g_async_queue_lock(nvenc->bitstream_pool);
  g_async_queue_lock(nvenc->holding_queue);

  while (g_async_queue_length_unlocked(nvenc->holding_queue) > 0) {
    GST_DEBUG("Encoder is drained so moving buffer from holding to bitstream queue.");
    g_async_queue_push_unlocked(nvenc->bitstream_queue, g_async_queue_pop_unlocked(nvenc->holding_queue));
  }
  GST_FIXME_OBJECT (nvenc, "stop bitstream reading thread properly");

  //while ((out_buf = g_async_queue_try_pop_unlocked (nvenc->bitstream_queue))) {
  //  GST_INFO_OBJECT (nvenc, "stole bitstream buffer %p from queue", out_buf);
  //  g_async_queue_push_unlocked (nvenc->bitstream_pool, out_buf);
  //}
  g_async_queue_push_unlocked (nvenc->bitstream_queue, SHUTDOWN_COOKIE);
  g_async_queue_unlock (nvenc->bitstream_pool);
  g_async_queue_unlock (nvenc->bitstream_queue);
  g_async_queue_unlock (nvenc->holding_queue);


  /* temporary unlock, so other thread can find and push frame */
  GST_VIDEO_ENCODER_STREAM_UNLOCK (nvenc);
  g_thread_join (nvenc->bitstream_thread);
  GST_VIDEO_ENCODER_STREAM_LOCK (nvenc);
  GST_DEBUG("Bitstream thread stopped.");
  nvenc->bitstream_thread = NULL;
  return TRUE;
}

static void
gst_nv_base_enc_reset_queues (D3DGstNvBaseEnc * nvenc, gboolean refill)
{
  gpointer ptr;
  guint i;

  GST_INFO_OBJECT (nvenc, "clearing queues");

  while ((ptr = g_async_queue_try_pop (nvenc->bitstream_queue))) {
    /* do nothing */
  }
  while ((ptr = g_async_queue_try_pop (nvenc->bitstream_pool))) {
    /* do nothing */
  }
  while ((ptr = g_async_queue_try_pop (nvenc->in_bufs_pool))) {
    /* do nothing */
  }

  if (refill) {
    GST_INFO_OBJECT (nvenc, "refilling buffer pools");
    for (i = 0; i < nvenc->n_bufs; ++i) {
      g_async_queue_push (nvenc->bitstream_pool, nvenc->input_bufs[i]);
      g_async_queue_push (nvenc->in_bufs_pool, nvenc->output_bufs[i]);
    }
  }
}

static void
gst_nv_base_enc_free_buffers (D3DGstNvBaseEnc * nvenc)
{
  NVENCSTATUS nv_ret;
  guint i;

  if (nvenc->encoder == NULL)
    return;

  gst_nv_base_enc_reset_queues (nvenc, FALSE);

  for (i = 0; i < nvenc->n_bufs; ++i) {
    NV_ENC_OUTPUT_PTR out_buf = nvenc->output_bufs[i];

    if (nvenc->gl_input) {
      struct gl_input_resource *in_gl_resource = nvenc->input_bufs[i];
      g_free (in_gl_resource);
    } else
    {
      NV_ENC_INPUT_PTR in_buf = (NV_ENC_INPUT_PTR) nvenc->input_bufs[i];

      GST_DEBUG_OBJECT (nvenc, "Destroying input buffer %p", in_buf);
      nv_ret = NvEncDestroyInputBuffer (nvenc->encoder, in_buf);
      if (nv_ret != NV_ENC_SUCCESS) {
        GST_ERROR_OBJECT (nvenc, "Failed to destroy input buffer %p, ret %d",
            in_buf, nv_ret);
      }
    }

    GST_DEBUG_OBJECT (nvenc, "Destroying output bitstream buffer %p", out_buf);
    nv_ret = NvEncDestroyBitstreamBuffer (nvenc->encoder, out_buf);
    if (nv_ret != NV_ENC_SUCCESS) {
      GST_ERROR_OBJECT (nvenc, "Failed to destroy output buffer %p, ret %d",
          out_buf, nv_ret);
    }
  }

  nvenc->n_bufs = 0;
  g_free (nvenc->output_bufs);
  nvenc->output_bufs = NULL;
  g_free (nvenc->input_bufs);
  nvenc->input_bufs = NULL;
}

static inline guint
_get_plane_width (GstVideoInfo * info, guint plane)
{
  if (GST_VIDEO_INFO_IS_YUV (info))
    /* For now component width and plane width are the same and the
     * plane-component mapping matches
     */
    return GST_VIDEO_INFO_COMP_WIDTH (info, plane);
  else                          /* RGB, GRAY */
    return GST_VIDEO_INFO_WIDTH (info);
}

static inline guint
_get_plane_height (GstVideoInfo * info, guint plane)
{
  if (GST_VIDEO_INFO_IS_YUV (info))
    /* For now component width and plane width are the same and the
     * plane-component mapping matches
     */
    return GST_VIDEO_INFO_COMP_HEIGHT (info, plane);
  else                          /* RGB, GRAY */
    return GST_VIDEO_INFO_HEIGHT (info);
}

static inline gsize
_get_frame_data_height (GstVideoInfo * info)
{
  gsize ret = 0;
  guint i;

  for (i = 0; i < GST_VIDEO_INFO_N_PLANES (info); i++) {
    ret += _get_plane_height (info, i);
  }

  return ret;
}

void
gst_nv_base_enc_set_max_encode_size (D3DGstNvBaseEnc * nvenc, guint max_width,
    guint max_height)
{
  nvenc->max_encode_width = max_width;
  nvenc->max_encode_height = max_height;
}

void
gst_nv_base_enc_get_max_encode_size (D3DGstNvBaseEnc * nvenc, guint * max_width,
    guint * max_height)
{
  *max_width = nvenc->max_encode_width;
  *max_height = nvenc->max_encode_height;
}

static gboolean
gst_nv_base_enc_set_format (GstVideoEncoder * enc, GstVideoCodecState * state)
{
  GST_INFO("SET FORMAT");
  D3DGstNvBaseEncClass *nvenc_class = GST_D3D_NV_BASE_ENC_GET_CLASS (enc);
  D3DGstNvBaseEnc *nvenc = GST_D3D_NV_BASE_ENC (enc);
  GstVideoInfo *info = &state->info;
  GstVideoCodecState *old_state = nvenc->input_state;
  NV_ENC_RECONFIGURE_PARAMS reconfigure_params = { 0, };
  NV_ENC_INITIALIZE_PARAMS init_params = { 0, };
  NV_ENC_INITIALIZE_PARAMS *params;
  NV_ENC_PRESET_CONFIG preset_config = { NV_ENC_PRESET_CONFIG_VER,{ NV_ENC_CONFIG_VER } };
  NVENCSTATUS nv_ret;

  g_atomic_int_set (&nvenc->reconfig, FALSE);

  if (old_state) {
    reconfigure_params.version = NV_ENC_RECONFIGURE_PARAMS_VER;
    params = &reconfigure_params.reInitEncodeParams;
  } else {
    params = &init_params;
  }

  params->version = NV_ENC_INITIALIZE_PARAMS_VER;
  params->encodeGUID = nvenc_class->codec_id;
  params->encodeWidth = GST_VIDEO_INFO_WIDTH (info);
  params->encodeHeight = GST_VIDEO_INFO_HEIGHT (info);

  {
    guint32 n_presets;
    GUID *presets;
    guint32 i;

    nv_ret =
        NvEncGetEncodePresetCount (nvenc->encoder,
        params->encodeGUID, &n_presets);
    if (nv_ret != NV_ENC_SUCCESS) {
      GST_ELEMENT_ERROR (nvenc, LIBRARY, SETTINGS, (NULL),
          ("Failed to get encoder presets"));
      return FALSE;
    }

    presets = g_new0 (GUID, n_presets);
    nv_ret =
        NvEncGetEncodePresetGUIDs (nvenc->encoder,
        params->encodeGUID, presets, n_presets, &n_presets);
    if (nv_ret != NV_ENC_SUCCESS) {
      GST_ELEMENT_ERROR (nvenc, LIBRARY, SETTINGS, (NULL),
          ("Failed to get encoder presets"));
      g_free (presets);
      return FALSE;
    }

    for (i = 0; i < n_presets; i++) {
      if (gst_nvenc_cmp_guid (presets[i], nvenc->selected_preset))
        break;
    }
    g_free (presets);
    if (i >= n_presets) {
      GST_ELEMENT_ERROR (nvenc, LIBRARY, SETTINGS, (NULL),
          ("Selected preset not supported"));
      return FALSE;
    }

    params->presetGUID = nvenc->selected_preset;
  }

  params->enablePTD = 1;
  if (!old_state) {
    /* this sets the required buffer size and the maximum allowed size on
     * subsequent reconfigures */
    /* FIXME: propertise this */
    params->maxEncodeWidth = GST_VIDEO_INFO_WIDTH (info);
    params->maxEncodeHeight = GST_VIDEO_INFO_HEIGHT (info);
    gst_nv_base_enc_set_max_encode_size (nvenc, params->maxEncodeWidth,
        params->maxEncodeHeight);
  } else {
    gint max_width, max_height;
    gst_nv_base_enc_get_max_encode_size (nvenc, &max_width, &max_height);

    if (GST_VIDEO_INFO_WIDTH (info) > max_width
        || GST_VIDEO_INFO_HEIGHT (info) > max_height) {
      GST_ELEMENT_ERROR (nvenc, STREAM, FORMAT, ("%s", "Requested stream "
              "size is larger than the maximum configured size"), (NULL));
      return FALSE;
    }
  }
  nv_ret =
      NvEncGetEncodePresetConfig (nvenc->encoder,
      params->encodeGUID, params->presetGUID, &preset_config);
  if (nv_ret != NV_ENC_SUCCESS) {
    GST_ELEMENT_ERROR (nvenc, LIBRARY, SETTINGS, (NULL),
        ("Failed to get encode preset configuration: %d", nv_ret));
    return FALSE;
  }

  params->encodeConfig = &preset_config.presetCfg;
  // TODO: Check encoder caps to make sure they support these settings.
  NV_ENC_CAPS_PARAM caps_param = { 0, };
  caps_param.version = NV_ENC_CAPS_PARAM_VER;
  caps_param.capsToQuery = NV_ENC_CAPS_SUPPORT_LOOKAHEAD;
  int supported = 0;

  if(NvEncGetEncodeCaps(nvenc->encoder, nvenc_class->codec_id,
    &caps_param, &supported) == NV_ENC_SUCCESS && supported) {
    GST_INFO("Enabling lookahead");
    //params->encodeConfig->rcParams.enableLookahead = 0;
    params->encodeConfig->rcParams.disableBadapt = 1; // disable b frames for now
    params->encodeConfig->rcParams.lookaheadDepth = 8;
  }

  caps_param.capsToQuery = NV_ENC_CAPS_SUPPORT_TEMPORAL_AQ;
  supported = 0;
  if (NvEncGetEncodeCaps(nvenc->encoder, nvenc_class->codec_id,
    &caps_param, &supported) == NV_ENC_SUCCESS && supported) {
    GST_INFO("Enabling temporal aq");
    //params->encodeConfig->rcParams.enableAQ = 1;
    //params->encodeConfig->rcParams.enableTemporalAQ = 1;
  }

  //caps_param.capsToQuery = NV_ENC_CAPS_SUPPORT_WEIGHTED_PREDICTION;
  //supported = 0;
  //if (NvEncGetEncodeCaps(nvenc->encoder, nvenc_class->codec_id,
  //  &caps_param, &supported) == NV_ENC_SUCCESS && supported) {
  //  GST_INFO("Enabling weighted prediction.");
  //  //params->enableWeightedPrediction = 1;
  //}

  //caps_param.capsToQuery = NV_ENC_CAPS_SUPPORT_BFRAME_REF_MODE;
  //supported = 0;
  //if (NvEncGetEncodeCaps(nvenc->encoder, nvenc_class->codec_id,
  //  &caps_param, &supported) == NV_ENC_SUCCESS && supported) {
  //  GST_INFO("Enabling useBFramesAsReF");
  //  params->encodeConfig->encodeCodecConfig.h264Config.useBFramesAsRef = 1;
  //}

  if (info->fps_d > 0 && info->fps_n > 0) {
    params->frameRateNum = info->fps_n;
    params->frameRateDen = info->fps_d;
  } else {
    GST_FIXME_OBJECT (nvenc, "variable framerate");
  }

  if (nvenc->rate_control_mode != GST_NV_RC_MODE_DEFAULT) {
    params->encodeConfig->rcParams.rateControlMode =
        _rc_mode_to_nv (nvenc->rate_control_mode);
    if (nvenc->bitrate > 0) {
      /* FIXME: this produces larger bitrates?! */
      params->encodeConfig->rcParams.averageBitRate = nvenc->bitrate * 1024;
      params->encodeConfig->rcParams.maxBitRate = nvenc->bitrate * 1024;
    }
    if (nvenc->qp_const > 0) {
      params->encodeConfig->rcParams.constQP.qpInterB = nvenc->qp_const;
      params->encodeConfig->rcParams.constQP.qpInterP = nvenc->qp_const;
      params->encodeConfig->rcParams.constQP.qpIntra = nvenc->qp_const;
    }
    if (nvenc->qp_min >= 0) {
      params->encodeConfig->rcParams.enableMinQP = 1;
      params->encodeConfig->rcParams.minQP.qpInterB = nvenc->qp_min;
      params->encodeConfig->rcParams.minQP.qpInterP = nvenc->qp_min;
      params->encodeConfig->rcParams.minQP.qpIntra = nvenc->qp_min;
    }
    if (nvenc->qp_max >= 0) {
      params->encodeConfig->rcParams.enableMaxQP = 1;
      params->encodeConfig->rcParams.maxQP.qpInterB = nvenc->qp_max;
      params->encodeConfig->rcParams.maxQP.qpInterP = nvenc->qp_max;
      params->encodeConfig->rcParams.maxQP.qpIntra = nvenc->qp_max;
    }
  }

  if (nvenc->gop_size < 0) {
    params->encodeConfig->gopLength = NVENC_INFINITE_GOPLENGTH;
    params->encodeConfig->frameIntervalP = 1;
  } else if (nvenc->gop_size > 0) {
    params->encodeConfig->gopLength = nvenc->gop_size;
  }

  g_assert (nvenc_class->set_encoder_config);
  if (!nvenc_class->set_encoder_config (nvenc, state, params->encodeConfig)) {
    GST_ERROR_OBJECT (enc, "Subclass failed to set encoder configuration");
    return FALSE;
  }

  G_LOCK (initialization_lock);
  if (old_state) {
    GST_INFO("Reconfiguring encoder");
    nv_ret = gl_NvEncReconfigureEncoder (nvenc->context, nvenc->encoder, &reconfigure_params);
  } else {
    GST_INFO("Initializing encoder");
    nv_ret = gl_NvEncInitializeEncoder (nvenc->context, nvenc->encoder, params);
  }
  G_UNLOCK (initialization_lock);

  if (nv_ret != NV_ENC_SUCCESS) {
    GST_ELEMENT_ERROR (nvenc, LIBRARY, SETTINGS, (NULL),
        ("Failed to %sinit encoder: %d", old_state ? "re" : "", nv_ret));
    return FALSE;
  }
  GST_INFO_OBJECT (nvenc, "configured encoder");

  if (!old_state) {
    nvenc->input_info = *info;
    nvenc->gl_input = FALSE;
  }

  if (nvenc->input_state)
    gst_video_codec_state_unref (nvenc->input_state);
  nvenc->input_state = gst_video_codec_state_ref (state);
  GST_INFO_OBJECT (nvenc, "configured encoder");


  /* now allocate some buffers only on first configuration */
  if (!old_state) {
#if HAVE_NVENC_GST_GL
    GstCapsFeatures *features;
#endif
    guint num_macroblocks, i;
    guint input_width, input_height;

    input_width = GST_VIDEO_INFO_WIDTH (info);
    input_height = GST_VIDEO_INFO_HEIGHT (info);

    num_macroblocks = (GST_ROUND_UP_16 (input_width) >> 4)
        * (GST_ROUND_UP_16 (input_height) >> 4);
    nvenc->n_bufs = (num_macroblocks >= 8160) ? 32 : 48;

    /* input buffers */
    nvenc->input_bufs = g_new0 (gpointer, nvenc->n_bufs);

    features = gst_caps_get_features (state->caps, 0);
    if (gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_GL_MEMORY)) {
      guint pixel_depth = 0;
      nvenc->gl_input = TRUE;

      for (i = 0; i < GST_VIDEO_INFO_N_COMPONENTS (info); i++) {
        pixel_depth += GST_VIDEO_INFO_COMP_DEPTH (info, i);
      }

      for (i = 0; i < nvenc->n_bufs; ++i) {
        struct gl_input_resource *in_gl_resource =
            g_new0 (struct gl_input_resource, 1);

        memset (&in_gl_resource->nv_resource, 0,
            sizeof (in_gl_resource->nv_resource));
        memset (&in_gl_resource->nv_mapped_resource, 0,
            sizeof (in_gl_resource->nv_mapped_resource));

        in_gl_resource->nv_resource.version = NV_ENC_REGISTER_RESOURCE_VER;
        in_gl_resource->nv_resource.resourceType =
          NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX;
        in_gl_resource->nv_resource.width = input_width;
        in_gl_resource->nv_resource.height = input_height;
        in_gl_resource->nv_resource.pitch = input_width;
        in_gl_resource->nv_resource.bufferFormat =
            gst_nvenc_get_nv_buffer_format (GST_VIDEO_INFO_FORMAT (info));

        nvenc->input_bufs[i] = in_gl_resource;
        g_async_queue_push (nvenc->in_bufs_pool, nvenc->input_bufs[i]);
      }

    } else
    {
      for (i = 0; i < nvenc->n_bufs; ++i) {
        NV_ENC_CREATE_INPUT_BUFFER cin_buf = { 0, };

        cin_buf.version = NV_ENC_CREATE_INPUT_BUFFER_VER;

        cin_buf.width = GST_ROUND_UP_32 (input_width);
        cin_buf.height = GST_ROUND_UP_32 (input_height);

        // cin_buf.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_CACHED;
        cin_buf.bufferFmt =
            gst_nvenc_get_nv_buffer_format (GST_VIDEO_INFO_FORMAT (info));

        nv_ret = gl_NvEncCreateInputBuffer (nvenc->context, nvenc->encoder, &cin_buf);

        if (nv_ret != NV_ENC_SUCCESS) {
          GST_WARNING_OBJECT (enc, "Failed to allocate input buffer: %d",
              nv_ret);
          /* FIXME: clean up */
          return FALSE;
        }

        nvenc->input_bufs[i] = cin_buf.inputBuffer;

        GST_INFO_OBJECT (nvenc, "allocated  input buffer %2d: %p", i,
            nvenc->input_bufs[i]);

        g_async_queue_push (nvenc->in_bufs_pool, nvenc->input_bufs[i]);
      }
    }

    /* output buffers */
    nvenc->output_bufs = g_new0 (NV_ENC_OUTPUT_PTR, nvenc->n_bufs);
    for (i = 0; i < nvenc->n_bufs; ++i) {
      NV_ENC_CREATE_BITSTREAM_BUFFER cout_buf = { 0, };

      cout_buf.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;

      /* 1 MB should be large enough to hold most output frames.
       * NVENC will automatically increase this if it's not enough. */
      cout_buf.size = 1024 * 1024;
      cout_buf.memoryHeap = NV_ENC_MEMORY_HEAP_SYSMEM_CACHED;

      G_LOCK (initialization_lock);
      nv_ret = gl_NvEncCreateBitstreamBuffer (nvenc->context, nvenc->encoder, &cout_buf);
      G_UNLOCK (initialization_lock);

      if (nv_ret != NV_ENC_SUCCESS) {
        GST_WARNING_OBJECT (enc, "Failed to allocate input buffer: %d", nv_ret);
        /* FIXME: clean up */
        return FALSE;
      }

      nvenc->output_bufs[i] = cout_buf.bitstreamBuffer;

      GST_INFO_OBJECT (nvenc, "allocated output buffer %2d: %p", i,
          nvenc->output_bufs[i]);

      g_async_queue_push (nvenc->bitstream_pool, nvenc->output_bufs[i]);
    }
  }

  g_assert (nvenc_class->set_src_caps);
  if (!nvenc_class->set_src_caps (nvenc, state)) {
    GST_ERROR_OBJECT (nvenc, "Subclass failed to set output caps");
    /* FIXME: clean up */
    return FALSE;
  }

  return TRUE;
}

static inline guint
_plane_get_n_components (GstVideoInfo * info, guint plane)
{
  switch (GST_VIDEO_INFO_FORMAT (info)) {
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_AYUV:
      return 4;
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_BGR16:
      return 3;
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_GRAY16_LE:
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
      return 2;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      return plane == 0 ? 1 : 2;
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      return 1;
    default:
      g_assert_not_reached ();
      return 1;
  }
}

//#if HAVE_NVENC_GST_GL
struct map_gl_input
{
  D3DGstNvBaseEnc *nvenc;
  GstVideoCodecFrame *frame;
  GstVideoInfo *info;
  struct gl_input_resource *in_gl_resource;
};

static GstFlowReturn
_acquire_input_buffer (D3DGstNvBaseEnc * nvenc, gpointer * input)
{
  g_assert (input);

  GST_LOG_OBJECT (nvenc, "acquiring input buffer..");
  GST_VIDEO_ENCODER_STREAM_UNLOCK (nvenc);
  *input = g_async_queue_pop (nvenc->in_bufs_pool);
  GST_VIDEO_ENCODER_STREAM_LOCK (nvenc);

  return GST_FLOW_OK;
}

static GstFlowReturn
_submit_input_buffer (D3DGstNvBaseEnc * nvenc, GstVideoCodecFrame * frame,
    void *inputBuffer, void *inputBufferPtr,
    NV_ENC_BUFFER_FORMAT bufferFormat, void *outputBufferPtr)
{
  D3DGstNvBaseEncClass *nvenc_class = GST_D3D_NV_BASE_ENC_GET_CLASS (nvenc);
  NV_ENC_PIC_PARAMS pic_params = { 0, };
  NVENCSTATUS nv_ret;

  GST_LOG_OBJECT (nvenc, "%u: input buffer %p, output buffer %p, "
      "pts %" GST_TIME_FORMAT, frame->system_frame_number, inputBuffer,
      outputBufferPtr, GST_TIME_ARGS (frame->pts));

  GstVideoMeta * meta = gst_buffer_get_video_meta(frame->input_buffer);
  
  pic_params.version = NV_ENC_PIC_PARAMS_VER;
  pic_params.inputBuffer = inputBufferPtr;
  pic_params.bufferFmt = bufferFormat;

  pic_params.inputWidth = meta->width;
  pic_params.inputHeight = meta->height;
  pic_params.outputBitstream = outputBufferPtr;
  pic_params.completionEvent = NULL;

  pic_params.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
  pic_params.inputTimeStamp = frame->pts;
  pic_params.inputDuration =
      GST_CLOCK_TIME_IS_VALID (frame->duration) ? frame->duration : 0;
  pic_params.frameIdx = frame->system_frame_number;

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame))
    pic_params.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR;
  else
    pic_params.encodePicFlags = 0;

  if (nvenc_class->set_pic_params
      && !nvenc_class->set_pic_params (nvenc, frame, &pic_params)) {
    GST_ERROR_OBJECT (nvenc, "Subclass failed to submit buffer");
    return GST_FLOW_ERROR;
  }

  nv_ret = gl_NvEncEncodePicture(nvenc->context, nvenc->encoder, &pic_params);
  if (nv_ret == NV_ENC_SUCCESS) {
    GST_LOG ("Encoded picture: %d", outputBufferPtr);
    void *b_frame_buffer = g_async_queue_try_pop(nvenc->holding_queue);
    if (b_frame_buffer) {
      g_async_queue_push(nvenc->bitstream_queue, b_frame_buffer);
      g_async_queue_push(nvenc->holding_queue, outputBufferPtr);
    }
    else {
      g_async_queue_push(nvenc->bitstream_queue, outputBufferPtr);
    }
  } else if (nv_ret == NV_ENC_ERR_NEED_MORE_INPUT) {
    /* FIXME: we should probably queue pending output buffers here and only
     * submit them to the async queue once we got sucess back */
    GST_DEBUG("Encoded picture (encoder needs more input) %d", outputBufferPtr);

    g_async_queue_push(nvenc->holding_queue, outputBufferPtr);
  } else {
    GST_ERROR_OBJECT (nvenc, "Failed to encode picture: %d", nv_ret);
    GST_DEBUG_OBJECT (nvenc, "re-enqueueing input buffer %p", inputBuffer);
    g_async_queue_push (nvenc->in_bufs_pool, inputBuffer);
    GST_DEBUG_OBJECT (nvenc, "re-enqueueing output buffer %p", outputBufferPtr);
    g_async_queue_push (nvenc->bitstream_pool, outputBufferPtr);

    return GST_FLOW_ERROR;
  }
  return GST_FLOW_OK;
}

static int gl_run_dxgi_map_d3d(GstGLContext *context, GstGLDXGIMemory * gl_mem)
{
  gl_dxgi_map_d3d(gl_mem);
}

static GstFlowReturn
gst_nv_base_enc_handle_frame (GstVideoEncoder * enc, GstVideoCodecFrame * frame)
{

  // FIXME - we should LOCK when we modify our data?!?
  gpointer input_buffer = NULL;
  D3DGstNvBaseEnc *nvenc = GST_D3D_NV_BASE_ENC (enc);

  GstGLDXGIMemory *gl_mem = (GstGLDXGIMemory *) gst_buffer_peek_memory (frame->input_buffer, 0);
  GST_LOG("handle_frame texture_id %#010x interop_id:%#010x status:%d",
      gl_mem->mem.tex_id,
      gl_mem->interop_handle,
      gl_mem->status);

  NV_ENC_OUTPUT_PTR out_buf;
  NVENCSTATUS nv_ret;
  GstVideoInfo *info = &nvenc->input_state->info;
  GstFlowReturn flow = GST_FLOW_OK;
  GstMapFlags in_map_flags = GST_MAP_READ;
  struct frame_state *state = NULL;
  guint frame_n = 0;
  g_assert (nvenc->encoder != NULL);
  gst_buffer_ref(frame->input_buffer);

  if (g_atomic_int_compare_and_exchange (&nvenc->reconfig, TRUE, FALSE)) {
    if (!gst_nv_base_enc_set_format (enc, nvenc->input_state))
      return GST_FLOW_ERROR;
  }

  /* make sure our thread that waits for output to be ready is started */
  if (nvenc->bitstream_thread == NULL) {
    if (!gst_nv_base_enc_start_bitstream_thread (nvenc))
      goto error;
  }

  flow = _acquire_input_buffer (nvenc, &input_buffer);
  if (flow != GST_FLOW_OK)
    goto out;
  if (input_buffer == NULL)
    goto error;

  state = frame->user_data;
  if (!state)
    state = g_new0 (struct frame_state, 1);
  state->n_buffers = 1;

  struct gl_input_resource *in_gl_resource = input_buffer;
  struct map_gl_input data;

  GST_LOG_OBJECT (enc, "got input buffer %p", in_gl_resource);
  GstClockTimeDiff PTS_DIFF = frame->pts - nvenc->last_frame_pts;

  if (nvenc->last_frame_number == 0) {
    nvenc->last_frame_pts = frame->pts;
    nvenc->last_frame_number = frame->presentation_frame_number;
  }

  if (PTS_DIFF >= GST_SECOND) {
    nvenc->fps = frame->presentation_frame_number - nvenc->last_frame_number;
    nvenc->last_frame_pts = frame->pts;
    nvenc->last_frame_number = frame->presentation_frame_number;
  }

  in_gl_resource->gl_mem[0] =
      (GstGLMemory *) gst_buffer_peek_memory (frame->input_buffer, 0);
  g_assert (gst_is_gl_memory ((GstMemory *) in_gl_resource->gl_mem[0]));

  data.nvenc = nvenc;
  data.frame = frame;
//  data.info = &vframe.info;
  data.in_gl_resource = in_gl_resource;

  in_gl_resource->nv_resource.resourceToRegister =
    ((GstGLDXGIMemory*)in_gl_resource->gl_mem[0])->d3d11texture;
  nv_ret =
      NvEncRegisterResource (nvenc->encoder,
      &in_gl_resource->nv_resource);
  if (nv_ret != NV_ENC_SUCCESS) {
    GST_ERROR_OBJECT(nvenc, "Failed to register resource %p, ret %d",
      in_gl_resource, nv_ret);
    return GST_FLOW_ERROR;
  }

  in_gl_resource->nv_mapped_resource.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
  in_gl_resource->nv_mapped_resource.registeredResource =
      in_gl_resource->nv_resource.registeredResource;

  nv_ret =
      NvEncMapInputResource (nvenc->encoder,
      &in_gl_resource->nv_mapped_resource);

  if (nv_ret != NV_ENC_SUCCESS) {
    GST_ERROR_OBJECT (nvenc, "Failed to map input resource %p, ret %d",
        in_gl_resource, nv_ret);
    goto error;
  }

  out_buf = g_async_queue_try_pop (nvenc->bitstream_pool);
  if (out_buf == NULL) {
    GST_DEBUG_OBJECT (nvenc, "wait for output buf to become available again");
    out_buf = g_async_queue_pop (nvenc->bitstream_pool);
  }

  state->in_bufs[frame_n] = in_gl_resource;
  state->out_bufs[frame_n++] = out_buf;

  frame->user_data = state;
  frame->user_data_destroy_notify = (GDestroyNotify) g_free;

  in_gl_resource->buf = frame->input_buffer;

  flow =
    _submit_input_buffer(nvenc, frame, in_gl_resource,
      in_gl_resource->nv_mapped_resource.mappedResource,
      in_gl_resource->nv_mapped_resource.mappedBufferFmt, out_buf);

  /* encoder will keep frame in list internally, we'll look it up again later
    * in the thread where we get the output buffers and finish it there */
  gst_video_codec_frame_unref (frame);
  frame = NULL;
  
  if (flow != GST_FLOW_OK)
    goto out;

  flow = g_atomic_int_get (&nvenc->last_flow);

out:

  return flow;

error:
  flow = GST_FLOW_ERROR;
  if (state)
    g_free (state);
  if (input_buffer)
    g_free (input_buffer);
  goto out;
}

static gboolean
gst_nv_base_enc_drain_encoder (D3DGstNvBaseEnc * nvenc)
{
  NV_ENC_PIC_PARAMS pic_params = { 0, };
  NVENCSTATUS nv_ret;

  GST_INFO_OBJECT (nvenc, "draining encoder");

  if (nvenc->input_state == NULL) {
    GST_DEBUG_OBJECT (nvenc, "no input state, nothing to do");
    return TRUE;
  }

  pic_params.version = NV_ENC_PIC_PARAMS_VER;
  pic_params.encodePicFlags = NV_ENC_PIC_FLAG_EOS;

  nv_ret = gl_NvEncEncodePicture (nvenc->context, nvenc->encoder, &pic_params);
  if (nv_ret != NV_ENC_SUCCESS) {
    GST_ERROR_OBJECT (nvenc, "Failed to drain encoder, ret %d", nv_ret);
    return FALSE;
  }
  GST_DEBUG("Encoder drained.");
  return TRUE;
}

static GstFlowReturn
gst_nv_base_enc_finish (GstVideoEncoder * enc)
{
  D3DGstNvBaseEnc *nvenc = GST_D3D_NV_BASE_ENC (enc);

  GST_FIXME_OBJECT (enc, "implement finish");

  //gst_nv_base_enc_drain_encoder (nvenc);

  /* wait for encoder to output the remaining buffers */
  //gst_nv_base_enc_stop_bitstream_thread (nvenc);

  return GST_FLOW_OK;
}

static void
gst_nv_base_enc_schedule_reconfig (D3DGstNvBaseEnc * nvenc)
{
  g_atomic_int_set (&nvenc->reconfig, TRUE);
}

static void
gst_nv_base_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  D3DGstNvBaseEnc *nvenc = GST_D3D_NV_BASE_ENC (object);

  switch (prop_id) {
    case PROP_DEVICE_ID:
      nvenc->cuda_device_id = g_value_get_uint (value);
      break;
    case PROP_PRESET:
      nvenc->preset_enum = g_value_get_enum (value);
      nvenc->selected_preset = _nv_preset_to_guid (nvenc->preset_enum);
      gst_nv_base_enc_schedule_reconfig (nvenc);
      break;
    case PROP_RC_MODE:
      nvenc->rate_control_mode = g_value_get_enum (value);
      gst_nv_base_enc_schedule_reconfig (nvenc);
      break;
    case PROP_QP_MIN:
      nvenc->qp_min = g_value_get_int (value);
      gst_nv_base_enc_schedule_reconfig (nvenc);
      break;
    case PROP_QP_MAX:
      nvenc->qp_max = g_value_get_int (value);
      gst_nv_base_enc_schedule_reconfig (nvenc);
      break;
    case PROP_QP_CONST:
      nvenc->qp_const = g_value_get_int (value);
      gst_nv_base_enc_schedule_reconfig (nvenc);
      break;
    case PROP_BITRATE:
      nvenc->bitrate = g_value_get_uint (value);
      gst_nv_base_enc_schedule_reconfig (nvenc);
      break;
    case PROP_GOP_SIZE:
      nvenc->gop_size = g_value_get_int (value);
      gst_nv_base_enc_schedule_reconfig (nvenc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_nv_base_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  D3DGstNvBaseEnc *nvenc = GST_D3D_NV_BASE_ENC (object);

  switch (prop_id) {
    case PROP_DEVICE_ID:
      g_value_set_uint (value, nvenc->cuda_device_id);
      break;
    case PROP_PRESET:
      g_value_set_enum (value, nvenc->preset_enum);
      break;
    case PROP_RC_MODE:
      g_value_set_enum (value, nvenc->rate_control_mode);
      break;
    case PROP_QP_MIN:
      g_value_set_int (value, nvenc->qp_min);
      break;
    case PROP_QP_MAX:
      g_value_set_int (value, nvenc->qp_max);
      break;
    case PROP_QP_CONST:
      g_value_set_int (value, nvenc->qp_const);
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, nvenc->bitrate);
      break;
    case PROP_GOP_SIZE:
      g_value_set_int (value, nvenc->gop_size);
      break;
    case PROP_FPS:
      g_value_set_int(value, nvenc->fps);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean gst_nv_base_enc_propose_allocation (GstVideoEncoder * enc, GstQuery * query)
{
  D3DGstNvBaseEnc *self= GST_D3D_NV_BASE_ENC (enc);
  GST_LOG_OBJECT(self, "gst_nv_base_enc_propose_allocation");
  GST_INFO("Proposing D3D11 texture allocation");
  GstCaps *caps;
  gboolean need_pool;
  gst_query_parse_allocation(query, &caps, &need_pool);
  GstCapsFeatures *features;
  features = gst_caps_get_features (caps, 0);
  gst_query_add_allocation_meta(query, GST_GL_SYNC_META_API_TYPE, 0);
  if (!gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY)) {
      GST_ERROR_OBJECT(self, "shouldn't GL MEMORY be negotiated?");
  } 

  GstVideoInfo info;
  if (!gst_video_info_from_caps (&info, caps))
    goto invalid_caps;

  guint vi_size = (guint) info.size;

  if (!gst_nv_base_enc_ensure_gl_context(self)) {
    return FALSE;
  }

  if (self->pool) {
    GstStructure *cur_pool_config;
    cur_pool_config = gst_buffer_pool_get_config(self->pool);
    guint size;
    gst_buffer_pool_config_get_params(cur_pool_config, NULL, &size, NULL, NULL);
    GST_DEBUG("Old pool size: %d New allocation size: info.size: %d", size, vi_size);
    if (size == vi_size) {
      GST_DEBUG("Reusing buffer pool.");
      gst_query_add_allocation_pool(query, self->pool, vi_size, BUFFER_COUNT, BUFFER_COUNT);
      return TRUE;
    } else {
      GST_DEBUG("The pool buffer size doesn't match (old: %d new: %d). Creating a new one.",
        size, vi_size);
      gst_object_unref(self->pool);
    }
  }

  // offer our custom allocator
  GstAllocator *allocator;
  GstAllocationParams params;
  gst_allocation_params_init(&params);

  allocator = GST_ALLOCATOR(self->allocator);
  gst_query_add_allocation_param(query, allocator, &params);
  gst_object_unref(allocator);
  GST_DEBUG("Make a new buffer pool.");
  self->pool = gst_gl_buffer_pool_new(self->context);
  GstStructure *config;
  config = gst_buffer_pool_get_config (self->pool);
  gst_buffer_pool_config_set_params (config, caps, vi_size, BUFFER_COUNT, BUFFER_COUNT);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_GL_SYNC_META);
  gst_buffer_pool_config_set_allocator (config, GST_ALLOCATOR (self->allocator), &params);

  if (!gst_buffer_pool_set_config (self->pool, config)) {
    gst_object_unref (self->pool);
    goto config_failed;
  }

  /* we need at least 2 buffer because we hold on to the last one */
  gst_query_add_allocation_pool (query, self->pool, vi_size, BUFFER_COUNT, BUFFER_COUNT);
  GST_DEBUG_OBJECT(self, "Added %" GST_PTR_FORMAT " pool to query", self->pool);

  return TRUE;

invalid_caps:
  {
    GST_WARNING_OBJECT (self, "invalid caps specified");
    return FALSE;
  }
config_failed:
  {
    GST_WARNING_OBJECT (self, "failed setting config");
    return FALSE;
  }
}
