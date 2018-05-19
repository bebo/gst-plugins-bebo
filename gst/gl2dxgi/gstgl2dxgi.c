/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#include <stdio.h>

#include <gst/gl/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>
#include <gst/gl/gstgldisplay.h>
#include <gst/video/gstvideometa.h>
#include "gstgl2dxgi.h"
#include "gstdxgidevice.h"

#define BUFFER_COUNT 20
#define SUPPORTED_GL_APIS GST_GL_API_OPENGL3

GST_DEBUG_CATEGORY_STATIC (gst_gl_2_dxgi_debug);
#define GST_CAT_DEFAULT gst_gl_2_dxgi_debug

#define gst_gl_2_dxgi_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGL2DXGI, gst_gl_2_dxgi,
    GST_TYPE_GL_BASE_FILTER,
    GST_DEBUG_CATEGORY_INIT (gst_gl_2_dxgi_debug, "gl2dxgi", 0,
        "gl2dxgi Element"););

/* static gboolean gst_gl_2_dxgi_get_unit_size (GstBaseTransform * trans, */
/*     GstCaps * caps, gsize * size); */
/* static GstCaps *_gst_gl_2_dxgi_transform_caps (GstBaseTransform * bt, */
/*     GstPadDirection direction, GstCaps * caps, GstCaps * filter); */
/* static gboolean _gst_gl_2_dxgi_set_caps (GstBaseTransform * bt, */
/*     GstCaps * in_caps, GstCaps * out_caps); */
static gboolean gst_gl_2_dxgi_filter_meta (GstBaseTransform * trans,
    GstQuery * query, GType api, const GstStructure * params);
static gboolean gst_gl_2_dxgi_propose_allocation (GstBaseTransform *
    bt, GstQuery * decide_query, GstQuery * query);
/* static gboolean _gst_gl_2_dxgi_decide_allocation (GstBaseTransform * */
/*     trans, GstQuery * query); */
/*static GstFlowReturn */
/* gst_gl_2_dxgi_prepare_output_buffer (GstBaseTransform * bt, */
/*     GstBuffer * buffer, GstBuffer ** outbuf); */
static GstFlowReturn gst_gl_2_dxgi_transform (GstBaseTransform * bt,
    GstBuffer * buffer, GstBuffer * outbuf);
static gboolean gst_gl_2_dxgi_stop (GstBaseTransform * bt);
static gboolean gst_gl_2_dxgi_start (GstBaseTransform * bt);
static void gst_gl_2_dxgi_set_context (GstElement * element,
    GstContext * context);

static GstStaticPadTemplate gst_gl_2_dxgi_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        "video/x-raw(memory:GLMemory), "
        "format = (string) RGBA, "
        "width = (int) [ 16, 4096 ], height = (int) [ 16, 2160 ], "
        "framerate = (fraction) [0, MAX]"
    ));
static GstStaticPadTemplate gst_gl_2_dxgi_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(
        "video/x-raw(memory:GLMemory), "
        "format = (string) RGBA, "
        "width = (int) [ 16, 4096 ], height = (int) [ 16, 2160 ], "
        "framerate = (fraction) [0, MAX]"
    ));


static void
gst_gl_2_dxgi_finalize (GObject * object)
{
    // FIXME 
  /* GstGL2DXGI *upload = GST_GL_2_DXGI (object); */

  /* if (upload->upload) */
  /*   gst_object_unref (upload->upload); */
  /* upload->upload = NULL; */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_gl_2_dxgi_class_init (GstGL2DXGIClass * klass)
{
  GstBaseTransformClass *bt_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  //GstCaps *caps;

  /* bt_class->transform_caps = _gst_gl_2_dxgi_transform_caps; */
  //bt_class->set_caps = _gst_gl_2_dxgi_set_caps;
  bt_class->filter_meta = gst_gl_2_dxgi_filter_meta;
  bt_class->propose_allocation = gst_gl_2_dxgi_propose_allocation;
  /* bt_class->decide_allocation = _gst_gl_2_dxgi_decide_allocation; */
  /* bt_class->get_unit_size = gst_gl_2_dxgi_get_unit_size; */
  /* bt_class->prepare_output_buffer = gst_gl_2_dxgi_prepare_output_buffer; */
  bt_class->transform = gst_gl_2_dxgi_transform;
  bt_class->stop = gst_gl_2_dxgi_stop;
  bt_class->start = gst_gl_2_dxgi_start;

  bt_class->passthrough_on_same_caps = FALSE;

  gst_element_class_add_static_pad_template (element_class,
      &gst_gl_2_dxgi_src_pad_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_gl_2_dxgi_sink_pad_template);

//  caps = gst_gl_upload_get_input_template_caps ();
  //gst_element_class_add_pad_template (element_class,
  //    gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps));
  //gst_caps_unref (caps);
  //gst_element_class_add_static_pad_template(element_class, &sinktemplate);
//  gst_element_class_add_static_pad_template(element_class, &srctemplate);

  gst_element_class_set_metadata (element_class,
      "OpenGL 2 DXGI ", "Filter/Video",
      "OpenGL D3D11 interop", "Pigs in Flight, Inc");

  element_class->set_context = GST_DEBUG_FUNCPTR (gst_gl_2_dxgi_set_context);

  gobject_class->finalize = gst_gl_2_dxgi_finalize;
}

static void gst_gl_2_dxgi_set_context(GstElement * element,
  GstContext * context)
{
  GstGL2DXGI *self = GST_GL_2_DXGI(element);
  GstGLBaseFilter *gl_base_filter = GST_GL_BASE_FILTER(self);

  gst_gl_handle_set_context (element, context,
      (GstGLDisplay **) & gl_base_filter->display,
      (GstGLContext **) & self->other_context);
  if (gl_base_filter->display)
    gst_gl_display_filter_gl_api (GST_GL_DISPLAY (gl_base_filter->display),
        SUPPORTED_GL_APIS);

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);

}

static void
gst_gl_2_dxgi_init (GstGL2DXGI * upload)
{
  gst_base_transform_set_prefer_passthrough (GST_BASE_TRANSFORM (upload), TRUE);
}
static gboolean
gst_gl_2_dxgi_start (GstBaseTransform * bt)
{
  GstGL2DXGI *self = GST_GL_2_DXGI (bt);
  GST_ERROR_OBJECT (self, "Starting");

  /* if (upload->upload) { */
  /*   gst_object_unref (upload->upload); */
  /*   upload->upload = NULL; */
  /* } */
  GstGLBaseFilter *gl_base_filter = GST_GL_BASE_FILTER(self);
  gst_gl_ensure_element_data (GST_ELEMENT (bt),
      (GstGLDisplay **) & gl_base_filter->display,
      (GstGLContext **) & self->other_context);
  self->allocator = gst_gl_dxgi_memory_allocator_new(self);

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (bt);
}

static gboolean
gst_gl_2_dxgi_stop (GstBaseTransform * bt)
{
  GstGL2DXGI *self = GST_GL_2_DXGI (bt);
  GST_ERROR_OBJECT (self, "Stopping");

  if (self->pool)
    gst_object_unref (self->pool);
  self->pool = NULL;

  if (self->allocator)
    gst_object_unref (self->allocator);
  self->allocator = NULL;

  return GST_BASE_TRANSFORM_CLASS (parent_class)->stop (bt);
}

/* static gboolean */
/* gst_gl_2_dxgi_get_unit_size (GstBaseTransform * trans, GstCaps * caps, */
/*     gsize * size) */
/* { */
/*   gboolean ret = FALSE; */
/*   GstVideoInfo info; */

/*   ret = gst_video_info_from_caps (&info, caps); */
/*   if (ret) */
/*     *size = GST_VIDEO_INFO_SIZE (&info); */

/*   return TRUE; */
/* } */

/* static GstCaps * */
/* _gst_gl_2_dxgi_transform_caps (GstBaseTransform * bt, */
/*     GstPadDirection direction, GstCaps * caps, GstCaps * filter) */
/* { */
/*   GstGLBaseFilter *base_filter = GST_GL_BASE_FILTER (bt); */
/*   GstGL2DXGI *upload = GST_GL_2_DXGI (bt); */
/*   GstGLContext *context; */

/*   if (base_filter->display && !gst_gl_base_filter_find_gl_context (base_filter)) */
/*     return NULL; */

/*   context = GST_GL_BASE_FILTER (bt)->context; */
/*   if (upload->upload == NULL) */
/*     upload->upload = gst_gl_upload_new (context); */

/*   return gst_gl_upload_transform_caps (upload->upload, context, direction, caps, */
/*       filter); */
/* } */

static gboolean
gst_gl_2_dxgi_filter_meta (GstBaseTransform * trans, GstQuery * query,
    GType api, const GstStructure * params)
{
  /* propose all metadata upstream */
  return TRUE;
}

/* static gboolean */
/* _gst_gl_2_dxgi_propose_allocation (GstBaseTransform * bt, */
/*     GstQuery * decide_query, GstQuery * query) */
/* { */
/*   GstGL2DXGI *upload = GST_GL_2_DXGI (bt); */
/*   GstGLContext *context = GST_GL_BASE_FILTER (bt)->context; */
/*   gboolean ret; */

/*   if (!upload->upload) */
/*     return FALSE; */
/*   if (!context) */
/*     return FALSE; */

/*   gst_gl_upload_set_context (upload->upload, context); */

/*   ret = GST_BASE_TRANSFORM_CLASS (parent_class)->propose_allocation (bt, */
/*       decide_query, query); */
/*   gst_gl_upload_propose_allocation (upload->upload, decide_query, query); */

/*   return ret; */
/* } */



static gboolean
_find_local_gl_context(GstGLBaseFilter * filter)
{
  if (gst_gl_query_local_gl_context(GST_ELEMENT(filter), GST_PAD_SRC,
    &filter->context))
    return TRUE;
  if (gst_gl_query_local_gl_context(GST_ELEMENT(filter), GST_PAD_SINK,
    &filter->context))
    return TRUE;
  return FALSE;
}

static gboolean
gst_gl2dxgi_ensure_gl_context(GstGL2DXGI * self) {
  GstGLBaseFilter * gl_base_filter = GST_GL_BASE_FILTER(self);
  return gst_dxgi_device_ensure_gl_context((GstElement *)self, &gl_base_filter->context, &self->other_context, &gl_base_filter->display);
}

static gboolean
gst_gl_2_dxgi_propose_allocation (GstBaseTransform * sink, GstQuery * decide_query, GstQuery * query)
{
  GstGL2DXGI *self = GST_GL_2_DXGI(sink);
  GST_ERROR_OBJECT(self, "gst_shm_sink_propose_allocation");
  GST_LOG_OBJECT(self, "propose_allocation");

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

  guint vi_size = (guint) info.size;

  if (!gst_gl2dxgi_ensure_gl_context(self)) {
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
      gst_query_add_allocation_pool(query, self->pool, vi_size, BUFFER_COUNT, 0);
      return TRUE;
    } else {
      GST_DEBUG("The pool buffer size doesn't match (old: %d new: %d). Creating a new one.",
        size, vi_size);
      gst_object_unref(self->pool);
    }
  }

  GST_DEBUG("Make a new buffer pool.");
  self->pool = gst_gl_buffer_pool_new(GST_GL_BASE_FILTER(self)->context);
  GstStructure *config;
  config = gst_buffer_pool_get_config (self->pool);
  gst_buffer_pool_config_set_params (config, caps, vi_size, 0, 0);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_GL_SYNC_META);
  gst_buffer_pool_config_set_allocator (config, GST_ALLOCATOR (self->allocator), &params);

  if (!gst_buffer_pool_set_config (self->pool, config)) {
    gst_object_unref (self->pool);
    goto config_failed;
  }

  /* we need at least 2 buffer because we hold on to the last one */
  gst_query_add_allocation_pool (query, self->pool, vi_size, BUFFER_COUNT, 0);
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

static gboolean
_gst_gl_2_dxgi_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
  return
      GST_BASE_TRANSFORM_CLASS
      (gst_gl_2_dxgi_parent_class)->decide_allocation (trans, query);
}

/* static gboolean */
/* _gst_gl_2_dxgi_set_caps (GstBaseTransform * bt, GstCaps * in_caps, */
/*     GstCaps * out_caps) */
/* { */
/*   GstGL2DXGI *upload = GST_GL_2_DXGI (bt); */

/*   return gst_gl_upload_set_caps (upload->upload, in_caps, out_caps); */
/* } */

/* GstFlowReturn */
/* gst_gl_2_dxgi_prepare_output_buffer (GstBaseTransform * bt, */
/*     GstBuffer * buffer, GstBuffer ** outbuf) */
/* { */
/*   GstGL2DXGI *upload = GST_GL_2_DXGI (bt); */
/*   GstGLUploadReturn ret; */
/*   GstBaseTransformClass *bclass; */

/*   bclass = GST_BASE_TRANSFORM_GET_CLASS (bt); */

/*   if (gst_base_transform_is_passthrough (bt)) { */
/*     *outbuf = buffer; */
/*     return GST_FLOW_OK; */
/*   } */

/*   if (!upload->upload) */
/*     return GST_FLOW_NOT_NEGOTIATED; */

/*   ret = gst_gl_upload_perform_with_buffer (upload->upload, buffer, outbuf); */
/*   if (ret == GST_GL_UPLOAD_RECONFIGURE) { */
/*     gst_base_transform_reconfigure_src (bt); */
/*     return GST_FLOW_OK; */
/*   } */

/*   if (ret != GST_GL_UPLOAD_DONE || *outbuf == NULL) { */
/*     GST_ELEMENT_ERROR (bt, RESOURCE, NOT_FOUND, ("%s", */
/*             "Failed to upload buffer"), (NULL)); */
/*     if (*outbuf) */
/*       gst_buffer_unref (*outbuf); */
/*     return GST_FLOW_ERROR; */
/*   } */

/*   /1* basetransform doesn't unref if they're the same *1/ */
/*   if (buffer == *outbuf) */
/*     gst_buffer_unref (*outbuf); */
/*   else */
/*     bclass->copy_metadata (bt, buffer, *outbuf); */

/*   return GST_FLOW_OK; */
/* } */

static GstFlowReturn
gst_gl_2_dxgi_transform (GstBaseTransform * bt, GstBuffer * buffer,
    GstBuffer * outbuf)
{
  return GST_FLOW_OK;
}
