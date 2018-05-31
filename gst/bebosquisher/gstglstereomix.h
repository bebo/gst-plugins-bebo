/*
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2014 Jan Schmidt <jan@noraisin.net>
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

#ifndef __GST_GL_STEREO_MIX_H__
#define __GST_GL_STEREO_MIX_H__

#include "gstglmixer.h"

G_BEGIN_DECLS

#define GST_TYPE_GL_STEREO_MIX (gst_gl_stereo_mix_get_type())
#define GST_GL_STEREO_MIX(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_STEREO_MIX, GstGLStereoMixBebo))
#define GST_GL_STEREO_MIX_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_STEREO_MIX, GstGLStereoMixBeboClass))
#define GST_IS_GL_STEREO_MIX(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_STEREO_MIX))
#define GST_IS_GL_STEREO_MIX_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_STEREO_MIX))
#define GST_GL_STEREO_MIX_GET_CLASS(obj) \
        (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_GL_STEREO_MIX,GstGLStereoMixBeboClass))

typedef struct _GstGLStereoMixBebo GstGLStereoMixBebo;
typedef struct _GstGLStereoMixBeboClass GstGLStereoMixBeboClass;
typedef struct _GstGLStereoMixPadBebo GstGLStereoMixPadBebo;
typedef struct _GstGLStereoMixPadBeboClass GstGLStereoMixPadBeboClass;

struct _GstGLStereoMixPadBebo
{
  GstGLMixerPadBebo mixer_pad;

  gboolean mapped;
  GstBuffer *current_buffer;
};

struct _GstGLStereoMixPadBeboClass
{
  GstGLMixerPadBeboClass mixer_pad_class;
};

#define GST_TYPE_GL_STEREO_MIX_PAD (gst_gl_stereo_mix_pad_get_type ())
GType gst_gl_stereo_mix_pad_get_type (void);


struct _GstGLStereoMixBebo
{
  GstGLMixerBebo mixer;

  GLuint out_tex_id;

  GstGLViewConvert *viewconvert;
  GstGLStereoDownmix downmix_mode;

  GstVideoInfo mix_info;

  GPtrArray *input_frames;
  GstBuffer *primary_out;
  GstBuffer *auxilliary_out;
};

struct _GstGLStereoMixBeboClass
{
    GstGLMixerBeboClass mixer_class;
};

GType gst_gl_stereo_mix_get_type(void);

G_END_DECLS
#endif /* __GST_GL_STEREO_MIX_H__ */
