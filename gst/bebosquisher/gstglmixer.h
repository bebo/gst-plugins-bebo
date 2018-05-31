/*
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
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

#ifndef __GST_GL_MIXER_H__
#define __GST_GL_MIXER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/gl/gl.h>
#include "gstglbasemixer.h"

G_BEGIN_DECLS

typedef struct _GstGLMixerBebo GstGLMixerBebo;
typedef struct _GstGLMixerBeboClass GstGLMixerBeboClass;
typedef struct _GstGLMixerPrivateBebo GstGLMixerPrivate;

#define GST_TYPE_GL_MIXER_PAD (gst_gl_mixer_pad_get_type())
#define GST_GL_MIXER_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_MIXER_PAD, GstGLMixerPadBebo))
#define GST_GL_MIXER_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_MIXER_PAD, GstGLMixerPadBeboClass))
#define GST_IS_GL_MIXER_PAD(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_MIXER_PAD))
#define GST_IS_GL_MIXER_PAD_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_MIXER_PAD))
#define GST_GL_MIXER_PAD_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_GL_MIXER_PAD,GstGLMixerPadBeboClass))

typedef struct _GstGLMixerPad GstGLMixerPadBebo;
typedef struct _GstGLMixerPadClass GstGLMixerPadBeboClass;

/* all information needed for one video stream */
struct _GstGLMixerPad
{
  GstGLBaseMixerPadBebo parent;

  guint current_texture;
};

struct _GstGLMixerPadClass
{
  GstGLBaseMixerPadBeboClass parent_class;
};

GType gst_gl_mixer_pad_get_type (void);

#define GST_TYPE_GL_MIXER (gst_gl_mixer_get_type())
#define GST_GL_MIXER(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_MIXER, GstGLMixerBebo))
#define GST_GL_MIXER_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_MIXER, GstGLMixerBeboClass))
#define GST_IS_GL_MIXER(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_MIXER))
#define GST_IS_GL_MIXER_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_MIXER))
#define GST_GL_MIXER_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_GL_MIXER,GstGLMixerBeboClass))

typedef gboolean (*GstGLMixerSetCaps) (GstGLMixerBebo* mixer,
  GstCaps* outcaps);
typedef void (*GstGLMixerReset) (GstGLMixerBebo *mixer);
typedef gboolean (*GstGLMixerProcessFunc) (GstGLMixerBebo *mix, GstBuffer *outbuf);
typedef gboolean (*GstGLMixerProcessTextures) (GstGLMixerBebo *mix, GstGLMemory *out_tex);

struct _GstGLMixerBebo
{
  GstGLBaseMixerBebo vaggregator;

  GstGLFramebuffer *fbo;

  GstCaps *out_caps;

  GstGLMixerPrivate *priv;
};

struct _GstGLMixerBeboClass
{
  GstGLBaseMixerBeboClass parent_class;

  GstGLMixerSetCaps set_caps;
  GstGLMixerReset reset;
  GstGLMixerProcessFunc process_buffers;
  GstGLMixerProcessTextures process_textures;
};

GType gst_gl_mixer_get_type(void);

gboolean gst_gl_mixer_process_textures (GstGLMixerBebo * mix, GstBuffer * outbuf);

G_END_DECLS
#endif /* __GST_GL_MIXER_H__ */
