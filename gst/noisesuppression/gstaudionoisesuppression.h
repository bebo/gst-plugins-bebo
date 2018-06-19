/* GStreamer audio filter example class
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) YEAR AUTHOR_NAME AUTHOR_EMAIL
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-plugin
 *
 * FIXME:Describe plugin here.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m audiotestsrc ! plugin ! autoaudiosink
 * ]|
 * </refsect2>
 */

#ifndef GST_AUDIO_NOISE_SUPPRESSION_H_
#define GST_AUDIO_NOISE_SUPPRESSION_H_

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <string.h>
#include <speex/speex_preprocess.h>

G_BEGIN_DECLS

typedef struct _GstAudioNoiseSuppression GstAudioNoiseSuppression;
typedef struct _GstAudioNoiseSuppressionClass GstAudioNoiseSuppressionClass;

/* These are boilerplate cast macros and type check macros */
#define GST_TYPE_AUDIO_NOISE_SUPPRESSION \
  (gst_audio_noise_suppression_get_type())
#define GST_AUDIO_NOISE_SUPPRESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUDIO_NOISE_SUPPRESSION,GstAudioNoiseSuppression))
#define GST_AUDIO_NOISE_SUPPRESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUDIO_NOISE_SUPPRESSION,GstAudioNoiseSuppressionClass))
#define GST_IS_AUDIO_NOISE_SUPPRESSION(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUDIO_NOISE_SUPPRESSION))
#define GST_IS_AUDIO_NOISE_SUPPRESSION_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUDIO_NOISE_SUPPRESSION))

struct _GstAudioNoiseSuppression
{
  GstAudioFilter filter;

  gint              noise_suppress;

  GstAudioConverter *converter_pcm;
  GstAudioConverter *converter_original;
  GstAudioInfo      *info_pcm;

  SpeexPreprocessState *preprocess_state;
};

struct _GstAudioNoiseSuppressionClass
{
  GstAudioFilterClass parent_class;
};

G_END_DECLS

GType gst_audio_noise_suppression_get_type (void);

#endif /* GST_AUDIO_NOISE_SUPPRESSION_H_ */
