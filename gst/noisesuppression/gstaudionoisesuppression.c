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
 launch -v -m audiotestsrc ! plugin ! autoaudiosink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstaudionoisesuppression.h"
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <string.h>
#include <math.h>

GST_DEBUG_CATEGORY_STATIC (gst_audio_noise_suppression_debug);
#define GST_CAT_DEFAULT gst_audio_noise_suppression_debug

G_DEFINE_TYPE (GstAudioNoiseSuppression, gst_audio_noise_suppression,
    GST_TYPE_AUDIO_FILTER);

// A fake infinity value (because real infinity may break some hosts)
#define FAKE_INFINITY (65536.0 * 65536.0)

// Check for infinity (with appropriate-ish tolerance)
#define IS_FAKE_INFINITY(value) (fabs(value-FAKE_INFINITY) < 1.0)

enum
{
  PROP_0,
  PROP_NOISE_SUPPRESS
};

static void gst_audio_noise_suppression_finalize (GObject * object);
static void gst_audio_noise_suppression_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_audio_noise_suppression_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_audio_noise_suppression_setup (GstAudioFilter * filter,
    const GstAudioInfo * info);
static GstFlowReturn gst_audio_noise_suppression_filter (GstBaseTransform * bt,
    GstBuffer * outbuf, GstBuffer * inbuf);
static GstFlowReturn
gst_audio_noise_suppression_filter_inplace (GstBaseTransform * base_transform,
    GstBuffer * buf);

/* For simplicity only support 16-bit pcm in native endianness for starters */
#define SUPPORTED_CAPS_STRING \
    GST_AUDIO_CAPS_MAKE(GST_AUDIO_NE(S16))

#define MIN_NOISE_SUPPRESS      -60
#define MAX_NOISE_SUPPRESS      0
#define DEFAULT_NOISE_SUPPRESS  -30

/* GObject vmethod implementations */
static void
gst_audio_noise_suppression_class_init (GstAudioNoiseSuppressionClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseTransformClass *btrans_class;
  GstAudioFilterClass *audio_filter_class;
  GstCaps *caps;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  btrans_class = (GstBaseTransformClass *) klass;
  audio_filter_class = (GstAudioFilterClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_audio_noise_suppression_debug, "noisesuppression", 0,
        "audio noisesuppression element");

  gobject_class->finalize = gst_audio_noise_suppression_finalize;
  gobject_class->set_property = gst_audio_noise_suppression_set_property;
  gobject_class->get_property = gst_audio_noise_suppression_get_property;

  /* this function will be called when the format is set before the
   * first buffer comes in, and whenever the format changes */
  audio_filter_class->setup = GST_DEBUG_FUNCPTR (gst_audio_noise_suppression_setup);

  /* here you set up functions to process data (either in place, or from
   * one input buffer to another output buffer); only one is required */
  // btrans_class->transform = GST_DEBUG_FUNCPTR (gst_audio_noise_suppression_filter);
  btrans_class->transform_ip = gst_audio_noise_suppression_filter_inplace;
  /* Set some basic metadata about your new element */
  gst_element_class_set_details_simple (element_class,
    "Noise Suppression",
    "Filter/Effect/Audio",
    "Noise suppression for audio sources using speexdsp",
    "Jake Loo <jake@bebo.com>");

  g_object_class_install_property (gobject_class,
      PROP_NOISE_SUPPRESS,
      g_param_spec_int ("noise-suppress",
        "Maximum attenuation of the noise in dB",
        "Maximum attenuation of the noise in dB (negative number)",
        MIN_NOISE_SUPPRESS, MAX_NOISE_SUPPRESS, DEFAULT_NOISE_SUPPRESS,
        G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  caps = gst_caps_from_string (SUPPORTED_CAPS_STRING);
  gst_audio_filter_class_add_pad_templates (audio_filter_class, caps);
  gst_caps_unref (caps);
}

static void
gst_audio_noise_suppression_init (GstAudioNoiseSuppression * filter)
{
  filter->preprocess_state = NULL;
  filter->noise_suppress = DEFAULT_NOISE_SUPPRESS;

  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (filter), TRUE);
  // gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (filter), FALSE);
}

static void
gst_audio_noise_suppression_finalize (GObject * object)
{
  GstAudioNoiseSuppression *filter = GST_AUDIO_NOISE_SUPPRESSION (object);

  if (filter->preprocess_state)
    speex_preprocess_state_destroy(filter->preprocess_state);
}

static void
gst_audio_noise_suppression_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioNoiseSuppression *filter = GST_AUDIO_NOISE_SUPPRESSION (object);

  GST_OBJECT_LOCK (filter);
  switch (prop_id) {
    case PROP_NOISE_SUPPRESS:
      filter->noise_suppress = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (filter);
}

static void
gst_audio_noise_suppression_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioNoiseSuppression *filter = GST_AUDIO_NOISE_SUPPRESSION (object);

  GST_OBJECT_LOCK (filter);
  switch (prop_id) {
    case PROP_NOISE_SUPPRESS:
      g_value_set_int (value, filter->noise_suppress);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (filter);
}

static gboolean
gst_audio_noise_suppression_setup (GstAudioFilter * base,
    const GstAudioInfo * info)
{
  GstAudioNoiseSuppression *filter = GST_AUDIO_NOISE_SUPPRESSION (base);
  GstAudioFormat fmt;
  gint chans, rate;

  rate = GST_AUDIO_INFO_RATE (info);
  chans = GST_AUDIO_INFO_CHANNELS (info);
  fmt = GST_AUDIO_INFO_FORMAT (info);

  GST_INFO_OBJECT (filter, "format %d (%s), rate %d, %d channels",
      fmt, GST_AUDIO_INFO_NAME (info), rate, chans);

  /* if any setup needs to be done (like memory allocated), do it here */

  /* The audio filter base class also saves the audio info in
   * GST_AUDIO_FILTER_INFO(filter) so it's automatically available
   * later from there as well */

  int frame_size_ms = 20;
  int frame_size = rate * frame_size_ms / 1000;
  filter->preprocess_state = speex_preprocess_state_init(frame_size, rate);

  return TRUE;
}

/* You may choose to implement either a copying filter or an
 * in-place filter (or both).  Implementing only one will give
 * full functionality, however, implementing both will cause
 * audiofilter to use the optimal function in every situation,
 * with a minimum of memory copies. */

static GstFlowReturn
gst_audio_noise_suppression_filter (GstBaseTransform * base_transform,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstAudioNoiseSuppression *filter = GST_AUDIO_NOISE_SUPPRESSION (base_transform);
  const GstAudioInfo* info = GST_AUDIO_FILTER_INFO(filter);
  GstMapInfo map_in;
  GstMapInfo map_out;

  GST_LOG_OBJECT (filter, "transform buffer");

  if (gst_buffer_map (inbuf, &map_in, GST_MAP_READ)) {
    if (gst_buffer_map (outbuf, &map_out, GST_MAP_WRITE)) {
      g_assert (map_out.size == map_in.size);

      memcpy (map_out.data, map_in.data, map_out.size);
      speex_preprocess_run (filter->preprocess_state,
        (spx_int16_t *) map_out.data);

      gst_buffer_unmap (outbuf, &map_out);
    }
    gst_buffer_unmap (inbuf, &map_in);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_audio_noise_suppression_filter_inplace (GstBaseTransform * base_transform,
    GstBuffer * buf)
{
  GstAudioNoiseSuppression *filter = GST_AUDIO_NOISE_SUPPRESSION (base_transform);
  GstFlowReturn flow = GST_FLOW_OK;
  GstMapInfo map;

  GST_LOG_OBJECT (filter, "transform buffer in place");

  speex_preprocess_ctl(filter->preprocess_state,
      SPEEX_PREPROCESS_SET_NOISE_SUPPRESS,
      &filter->noise_suppress);

  if (gst_buffer_map (buf, &map, GST_MAP_READWRITE)) {
    speex_preprocess_run (filter->preprocess_state,
        (spx_int16_t *) map.data);

    gst_buffer_unmap (buf, &map);
  }

  return flow;
}


