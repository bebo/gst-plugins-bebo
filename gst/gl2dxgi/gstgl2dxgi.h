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

#ifndef __GST_GL_2_DXGI_H__
#define __GST_GL_2_DXGI_H__

#include <gst/video/video.h>

#include "gstdxgimemory.h"
/* #include <gst/gl/gstgl_fwd.h> */

G_BEGIN_DECLS

#define GST_TYPE_GL_2_DXGI (gst_gl_2_dxgi_get_type())
#define GST_GL_2_DXGI_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_GL_2_DXGI, GstGL2DXGIClass))
#define GST_GL_2_DXGI(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_2_DXGI,GstGL2DXGI))
#define GST_GL_2_DXGI_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_2_DXGI, GstGL2DXGIClass))
#define GST_IS_GL_2_DXGI(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_2_DXGI))
#define GST_IS_GL_2_DXGI_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_2_DXGI))
#define GST_GL_2_DXGI_CAST(obj) ((GstGL2DXGI*)(obj))

typedef struct _GstGL2DXGI GstGL2DXGI;
typedef struct _GstGL2DXGIClass GstGL2DXGIClass;
typedef struct _GstGL2DXGIPrivate GstGL2DXGIPrivate;

/**
 * GstGL2DXGI
 *
 * Opaque #GstGL2DXGI object
 */
struct _GstGL2DXGI
{
  /* <private> */
  GstGLBaseFilter parent;
  GstGLContext *other_context; /* context and display live in the base */
  GstBufferPool *pool;
  GstGLDXGIMemoryAllocator *allocator;
  GAsyncQueue   *queue;
  GstVideoInfo       in_info;
  GstVideoInfo       out_info;
  GstCaps           *out_caps;
  GstGLTextureTarget in_texture_target;
  GstGLTextureTarget out_texture_target;
  guint64 max_latency;
  guint64 latency;
  guint64 delay;
  guint64 max_delay;


  //GstGLUpload *upload;
};

/**
 * GstGL2DXGIClass:
 *
 * The #GstGL2DXGIClass struct only contains private data
 */
struct _GstGL2DXGIClass
{
  GstGLBaseFilterClass object_class;
  gboolean(*set_caps)          (GstGL2DXGI* gl2dxgi, GstCaps* incaps, GstCaps* outcaps);

};

G_END_DECLS

GType gst_gl_2_dxgi_get_type (void);

#endif /* __GST_GL_2_DXGI_H__ */
