/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
 * Copyright (C) 2018 Pigs in Flight, Inc
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

#include <windows.h>
#include <GL/gl.h>
#include <gst/gl/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>
#include <gst/gl/gstgldisplay.h>
#include <gst/video/gstvideometa.h>
#include "gstbufferholder.h"
#include "gstdxgidevice.h"

#define DEFAULT_SIZE_BUFFERS 5

GST_DEBUG_CATEGORY_STATIC (gst_buffer_holder_debug);
#define GST_CAT_DEFAULT gst_buffer_holder_debug

#define gst_buffer_holder_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstBufferHolder, gst_buffer_holder,
    GST_TYPE_BASE_TRANSFORM,
    GST_DEBUG_CATEGORY_INIT (gst_buffer_holder_debug, "bufferholder", 0,
        "bufferholder element"););

enum
{
  PROP_0,
  PROP_SIZE_BUFFERS
};

static GstFlowReturn gst_buffer_holder_prepare_output_buffer (GstBaseTransform * bt, 
    GstBuffer * buffer, GstBuffer ** outbuf);
static gboolean gst_buffer_holder_stop (GstBaseTransform * bt);
static gboolean gst_buffer_holder_start (GstBaseTransform * bt);
static gboolean gst_buffer_holder_accept_caps(GstBaseTransform * base,
  GstPadDirection direction, GstCaps * caps);
static void gst_buffer_holder_set_property(GObject * object, guint prop_id,
  const GValue * value, GParamSpec * pspec);
static void gst_buffer_holder_get_property(GObject * object, guint prop_id,
  GValue * value, GParamSpec * pspec);
static gboolean
gst_buffer_holder_query(GstBaseTransform * base, GstPadDirection direction,
  GstQuery * query);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE("sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE("src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS_ANY);

static void
gst_gl_2_dxgi_finalize (GObject * object)
{
    // FIXME 
  /* GstBufferHolder *upload = GST_BUFFER_HOLDER (object); */

  /* if (upload->upload) */
  /*   gst_object_unref (upload->upload); */
  /* upload->upload = NULL; */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void gst_buffer_holder_set_property(GObject * object, guint prop_id,
  const GValue * value, GParamSpec * pspec) {

  GstBufferHolder *self = GST_BUFFER_HOLDER(object);

  switch (prop_id) {
  case PROP_SIZE_BUFFERS:
    self->size_buffers = g_value_get_uint(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
    break;
  }
}

static void gst_buffer_holder_get_property(GObject * object, guint prop_id,
  GValue * value, GParamSpec * pspec) {

  GstBufferHolder *self = GST_BUFFER_HOLDER(object);
  GST_OBJECT_LOCK(self);

  switch (prop_id) {
    case PROP_SIZE_BUFFERS:
      g_value_set_uint64(value, self->size_buffers);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK(self);
}

static gboolean
gst_gl_2_dxgi_get_unit_size(GstBaseTransform * trans, GstCaps * caps,
  gsize * size)
{
  gboolean ret = FALSE;
  GstVideoInfo info;

  ret = gst_video_info_from_caps(&info, caps);
  if (ret)
    *size = GST_VIDEO_INFO_SIZE(&info);

  return TRUE;
}

static void
gst_buffer_holder_class_init (GstBufferHolderClass * klass)
{
  GstBaseTransformClass *bt_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  bt_class->prepare_output_buffer = GST_DEBUG_FUNCPTR (gst_buffer_holder_prepare_output_buffer);
  bt_class->stop = GST_DEBUG_FUNCPTR (gst_buffer_holder_stop);
  bt_class->start = GST_DEBUG_FUNCPTR (gst_buffer_holder_start);
  bt_class->query = GST_DEBUG_FUNCPTR (gst_buffer_holder_query);
  bt_class->accept_caps = GST_DEBUG_FUNCPTR(gst_buffer_holder_accept_caps);
  gst_element_class_add_static_pad_template (element_class,
      &srctemplate);
  gst_element_class_add_static_pad_template (element_class,
      &sinktemplate);

  gst_element_class_set_metadata (element_class,
      "OpenGL 2 DXGI ", "Filter/Video",
      "OpenGL D3D11 interop", "Pigs in Flight, Inc");

  gobject_class->finalize = gst_gl_2_dxgi_finalize;
  gobject_class->set_property = gst_buffer_holder_set_property;
  gobject_class->get_property = gst_buffer_holder_get_property;

  g_object_class_install_property(gobject_class,
    PROP_SIZE_BUFFERS,
    g_param_spec_uint64("size-buffers",
      "size in buffers",
      "the number of buffers to hold before pushing buffers downstream",
      0, G_MAXULONG, DEFAULT_SIZE_BUFFERS, G_PARAM_READWRITE));
}

static void
gst_buffer_holder_init (GstBufferHolder * self)
{
  GST_INFO("BUFFER BUFFER");
  gst_base_transform_set_prefer_passthrough (GST_BASE_TRANSFORM (self), FALSE);
  self->queue = g_async_queue_new();
  self->size_buffers = DEFAULT_SIZE_BUFFERS;
}

static gboolean
gst_buffer_holder_start (GstBaseTransform * bt)
{
  GstBufferHolder *self = GST_BUFFER_HOLDER (bt);
  GST_INFO_OBJECT (self, "Starting");

  return TRUE;
}

static gboolean
gst_buffer_holder_stop (GstBaseTransform * bt)
{
  GstBufferHolder *self = GST_BUFFER_HOLDER (bt);
  GST_DEBUG_OBJECT (self, "Stopping");

  if (self->queue) {
    GstBuffer * buf = g_async_queue_try_pop(self->queue);
    while (buf) {
      gst_buffer_unref(buf);
      buf = g_async_queue_try_pop(self->queue);
    }
    g_async_queue_unref(self->queue);
  }
  return TRUE;
}

GstFlowReturn 
gst_buffer_holder_prepare_output_buffer(GstBaseTransform * bt,
  GstBuffer * buffer, GstBuffer ** outbuf)
{
  GST_LOG("PREPARE");
  GstBufferHolder *self = GST_BUFFER_HOLDER(bt);
  g_async_queue_push(self->queue, buffer);
  gst_buffer_ref(buffer);

  if (g_async_queue_length(self->queue) < self->size_buffers) {
    return GST_BASE_TRANSFORM_FLOW_DROPPED;
  }
  GstBuffer * buf = g_async_queue_try_pop(self->queue);
  gst_buffer_unref(buf);
  *outbuf = buf;
  return GST_FLOW_OK;
}

static gboolean
gst_buffer_holder_accept_caps(GstBaseTransform * base,
  GstPadDirection direction, GstCaps * caps)
{
  gboolean ret;
  GstPad *pad;

  /* Proxy accept-caps */

  if (direction == GST_PAD_SRC)
    pad = GST_BASE_TRANSFORM_SINK_PAD(base);
  else
    pad = GST_BASE_TRANSFORM_SRC_PAD(base);

  ret = gst_pad_peer_query_accept_caps(pad, caps);

  return ret;
}

static gboolean
gst_buffer_holder_query(GstBaseTransform * base, GstPadDirection direction,
  GstQuery * query)
{
  GstBufferHolder *identity;
  gboolean ret;

  identity = GST_BUFFER_HOLDER(base);

  if (GST_QUERY_TYPE(query) == GST_QUERY_ALLOCATION) {
    GST_DEBUG_OBJECT(identity, "Dropping allocation query.");
    return FALSE;
  }

  ret = GST_BASE_TRANSFORM_CLASS(parent_class)->query(base, direction, query);

  if (GST_QUERY_TYPE(query) == GST_QUERY_LATENCY) {
    gboolean live = FALSE;
    GstClockTime min = 0, max = 0;

    if (ret) {
      gst_query_parse_latency(query, &live, &min, &max);

    }
    ret = TRUE;
  }
  return ret;
}
