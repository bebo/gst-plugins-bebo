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

#include "gstaudionoisegate.h"
#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/gstaudiofilter.h>
#include <string.h>
#include <math.h>

GST_DEBUG_CATEGORY_STATIC (gst_audio_noise_gate_debug);
#define GST_CAT_DEFAULT gst_audio_noise_gate_debug

G_DEFINE_TYPE (GstAudioNoiseGate, gst_audio_noise_gate,
    GST_TYPE_AUDIO_FILTER);

enum
{
  PROP_0,
  PROP_OPEN_THRESHOLD,
  PROP_CLOSE_THRESHOLD,
  PROP_ATTACK,
  PROP_RELEASE,
  PROP_ATTACK_HOLD_TIME,
  PROP_RELEASE_HOLD_TIME,
  PROP_MAKEUP
};

static void gst_audio_noise_gate_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_audio_noise_gate_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void reconfigure_values(GstAudioNoiseGate *filter);

static gboolean gst_audio_noise_gate_setup (GstAudioFilter * filter,
    const GstAudioInfo * info);
static GstFlowReturn gst_audio_noise_gate_filter (GstBaseTransform * bt,
    GstBuffer * outbuf, GstBuffer * inbuf);
static GstFlowReturn
gst_audio_noise_gate_filter_inplace (GstBaseTransform * base_transform,
    GstBuffer * buf);

static void gate_int16(GstAudioNoiseGate *s,
                 const gint16 *src, gint16 *dst, const gint16 *scsrc,
                 int nb_samples, double level_in, double level_sc);
static void gate_float(GstAudioNoiseGate *s,
                 const gfloat *src, gfloat *dst, const gfloat *scsrc,
                 int nb_samples, double level_in, double level_sc);

#if 0
/* This means we support signed 16-bit pcm and signed 32-bit pcm in native
 * endianness */
#define SUPPORTED_CAPS_STRING \
    GST_AUDIO_CAPS_MAKE("{ " GST_AUDIO_NE(S16) ", " GST_AUDIO_NE(S32) " }")
#endif

/* For simplicity only support 16-bit pcm in native endianness for starters */
#define SUPPORTED_CAPS_STRING \
    GST_AUDIO_CAPS_MAKE(GST_AUDIO_NE(F32))

#define DEFAULT_ATTACK            20
#define DEFAULT_RELEASE           250
#define DEFAULT_ATTACK_HOLD_TIME  0.0
#define DEFAULT_RELEASE_HOLD_TIME 200.0
#define DEFAULT_OPEN_THRESHOLD    -26.0
#define DEFAULT_CLOSE_THRESHOLD   0.0
#define DEFAULT_MAKEUP            1.0

static float
decibel_to_linear(float db)
{
  return powf(10.0f, (db / 20.0f));
}

static float
ms_to_s(float ms)
{
  return ms / 1000.0f;
}


/* GObject vmethod implementations */
static void
gst_audio_noise_gate_class_init (GstAudioNoiseGateClass * klass)
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

  GST_DEBUG_CATEGORY_INIT (gst_audio_noise_gate_debug, "noisegate", 0,
        "audio noisegate element");

  gobject_class->set_property = gst_audio_noise_gate_set_property;
  gobject_class->get_property = gst_audio_noise_gate_get_property;

  g_object_class_install_property (gobject_class, PROP_ATTACK,
      g_param_spec_int ("attack", "Attack",
          "Amount of milliseconds the signal has to rise above the threshold before gain reduction stops (ms)",
          0, 10000,
          DEFAULT_ATTACK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RELEASE,
      g_param_spec_int ("release", "Release",
          "Amount of milliseconds the signal has to fall below the threshold before the reduction is increased again (ms)",
          0, 10000,
          DEFAULT_RELEASE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_OPEN_THRESHOLD,
      g_param_spec_float ("open-threshold", "Open Threshold",
          "If a signal rises above this level the gain reduction is released (dB)", -100.0, 0.0,
          DEFAULT_OPEN_THRESHOLD,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CLOSE_THRESHOLD,
      g_param_spec_float ("close-threshold", "Close Threshold",
          "If a signal drop below this level the signal will be cut off (dB)", -100.0, 0.0,
          DEFAULT_CLOSE_THRESHOLD,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ATTACK_HOLD_TIME,
      g_param_spec_float ("attack-hold-time", "Attack Hold Time",
          "Hold time before starting to increase the signal gain (ms)", 0.0, 10000.0,
          DEFAULT_ATTACK_HOLD_TIME,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RELEASE_HOLD_TIME,
      g_param_spec_float ("release-hold-time", "Release Hold Time",
          "Hold time before starting to decrease the signal gain (ms)", 0.0, 10000.0,
          DEFAULT_RELEASE_HOLD_TIME,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAKEUP,
      g_param_spec_float ("makeup", "Makeup",
          "Set amount of amplification of signal after processing", 1.0, 64.0,
          DEFAULT_MAKEUP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* this function will be called when the format is set before the
   * first buffer comes in, and whenever the format changes */
  audio_filter_class->setup = GST_DEBUG_FUNCPTR (gst_audio_noise_gate_setup);

  /* here you set up functions to process data (either in place, or from
   * one input buffer to another output buffer); only one is required */
  btrans_class->transform = GST_DEBUG_FUNCPTR (gst_audio_noise_gate_filter);
  // btrans_class->transform_ip = gst_audio_noise_gate_filter_inplace;
  /* Set some basic metadata about your new element */
  gst_element_class_set_details_simple (element_class,
    "NoiseGate",
    "Filter/Effect/Audio",
    "NoiseGate for audio sources",
    "Jake Loo <jake@bebo.com>");

  caps = gst_caps_from_string (SUPPORTED_CAPS_STRING);
  gst_audio_filter_class_add_pad_templates (audio_filter_class, caps);
  gst_caps_unref (caps);
}

static void
gst_audio_noise_gate_init (GstAudioNoiseGate * filter)
{
  filter->attack = DEFAULT_ATTACK;
  filter->release = DEFAULT_RELEASE;
  filter->open_threshold_db = DEFAULT_OPEN_THRESHOLD;
  filter->close_threshold_db = DEFAULT_CLOSE_THRESHOLD;
  filter->attack_hold_time = DEFAULT_ATTACK_HOLD_TIME;
  filter->release_hold_time = DEFAULT_RELEASE_HOLD_TIME;
  filter->makeup = DEFAULT_MAKEUP;
  filter->previous_gain = 0.0;
  filter->hold_release_counter = 0.0;
  filter->hold_attack_counter = 0.0;

  // gst_base_transform_set_in_place (GST_BASE_TRANSFORM (filter), FALSE);
  // gst_base_transform_set_gap_aware (GST_BASE_TRANSFORM (filter), FALSE);
}

static void
gst_audio_noise_gate_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAudioNoiseGate *filter = GST_AUDIO_NOISE_GATE (object);

  GST_OBJECT_LOCK (filter);
  switch (prop_id) {
    case PROP_OPEN_THRESHOLD:
      filter->open_threshold_db = g_value_get_float (value);
      break;
    case PROP_CLOSE_THRESHOLD:
      filter->close_threshold_db = g_value_get_float (value);
      break;
    case PROP_ATTACK_HOLD_TIME:
      filter->attack_hold_time = g_value_get_float (value);
      break;
    case PROP_RELEASE_HOLD_TIME:
      filter->release_hold_time = g_value_get_float (value);
      break;
    case PROP_ATTACK:
      filter->attack = g_value_get_int (value);
      break;
    case PROP_RELEASE:
      filter->release = g_value_get_int (value);
      break;
    case PROP_MAKEUP:
      filter->makeup = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  reconfigure_values(filter);
  GST_OBJECT_UNLOCK (filter);
}

static void
gst_audio_noise_gate_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioNoiseGate *filter = GST_AUDIO_NOISE_GATE (object);

  GST_OBJECT_LOCK (filter);
  switch (prop_id) {
    case PROP_OPEN_THRESHOLD:
      g_value_set_float(value, filter->open_threshold_db);
      break;
    case PROP_CLOSE_THRESHOLD:
      g_value_set_float(value, filter->close_threshold_db);
      break;
    case PROP_ATTACK_HOLD_TIME:
      g_value_set_float(value, filter->attack_hold_time);
      break;
    case PROP_RELEASE_HOLD_TIME:
      g_value_set_float(value, filter->release_hold_time);
      break;
    case PROP_ATTACK:
      g_value_set_int(value, filter->attack);
      break;
    case PROP_RELEASE:
      g_value_set_int(value, filter->release);
      break;
    case PROP_MAKEUP:
      g_value_set_float(value, filter->makeup);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (filter);
}

static void
reconfigure_values(GstAudioNoiseGate *filter)
{
  const GstAudioInfo* info = GST_AUDIO_FILTER_INFO(filter);
  gint rate = GST_AUDIO_INFO_RATE (info);

  filter->attack_coeff  = expf(-logf(9) / (filter->attack / 1000.0f * rate));
  filter->release_coeff = expf(-logf(9) / (filter->release / 1000.0f * rate));
}

static gboolean
gst_audio_noise_gate_setup (GstAudioFilter * base,
    const GstAudioInfo * info)
{
  GstAudioNoiseGate *filter = GST_AUDIO_NOISE_GATE (base);
  GstAudioFormat fmt;
  gint chans, rate;

  rate = GST_AUDIO_INFO_RATE (info);
  chans = GST_AUDIO_INFO_CHANNELS (info);
  fmt = GST_AUDIO_INFO_FORMAT (info);

  GST_DEBUG_OBJECT (filter, "format %d (%s), rate %d, %d channels",
      fmt, GST_AUDIO_INFO_NAME (info), rate, chans);

  reconfigure_values(filter);

  // only support F32 now
  filter->process = (GstAudioNoiseGateProcessFunc) gate_float;

  return TRUE;
}

/* You may choose to implement either a copying filter or an
 * in-place filter (or both).  Implementing only one will give
 * full functionality, however, implementing both will cause
 * audiofilter to use the optimal function in every situation,
 * with a minimum of memory copies. */
static GstFlowReturn
gst_audio_noise_gate_filter (GstBaseTransform * base_transform,
    GstBuffer * inbuf, GstBuffer * outbuf)
{
  GstAudioNoiseGate *filter = GST_AUDIO_NOISE_GATE (base_transform);
  const GstAudioInfo* info = GST_AUDIO_FILTER_INFO(filter);
  GstMapInfo map_in;
  GstMapInfo map_out;

  if (gst_buffer_map (inbuf, &map_in, GST_MAP_READ)) {
    if (gst_buffer_map (outbuf, &map_out, GST_MAP_WRITE)) {
      g_assert (map_out.size == map_in.size);

      gint nbsamples = map_in.size / GST_AUDIO_INFO_BPF(info);
      filter->process(filter,
          map_in.data, map_out.data, map_in.data,
          nbsamples, 1.0, 1.0);

      gst_buffer_unmap (outbuf, &map_out);
    }
    gst_buffer_unmap (inbuf, &map_in);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_audio_noise_gate_filter_inplace (GstBaseTransform * base_transform,
    GstBuffer * buf)
{
  GstAudioNoiseGate *filter = GST_AUDIO_NOISE_GATE (base_transform);
  GstFlowReturn flow = GST_FLOW_OK;
  GstMapInfo map;

  if (gst_buffer_map (buf, &map, GST_MAP_READWRITE)) {
    gst_buffer_unmap (buf, &map);
  }

  return flow;
}


static void gate_float(GstAudioNoiseGate *s,
    const gfloat *src, gfloat *dst, const gfloat *scsrc,
    int nb_samples, double level_in, double level_sc)
{
  const GstAudioInfo* info = GST_AUDIO_FILTER_INFO(s);
  const gfloat open_threshold = decibel_to_linear(s->open_threshold_db);
  const gfloat close_threshold = MIN(open_threshold, decibel_to_linear(s->close_threshold_db));
  const gfloat makeup = s->makeup;
  const gfloat attack_coeff = s->attack_coeff;
  const gfloat release_coeff = s->release_coeff;
  const gint rate = GST_AUDIO_INFO_RATE (info);
  const gfloat attack_hold_time = ms_to_s(s->attack_hold_time);
  const gfloat release_hold_time = ms_to_s(s->release_hold_time);
  const gfloat period = 1.0f / rate;
  int n, c;
  int in_channels, out_channels;

  in_channels = out_channels = GST_AUDIO_INFO_CHANNELS(info);

  for (n = 0; n < nb_samples; n++, src += in_channels, dst += in_channels, scsrc += out_channels) {
    gfloat abs_sample = fabsf(scsrc[0]), gain = 1.0f;

    // find the peak
    for (c = 1; c < out_channels; c++) {
      abs_sample = MAX(fabsf(scsrc[c]), abs_sample);
    }

    gboolean gate_open = (abs_sample > open_threshold);
    gboolean gate_close = (abs_sample < close_threshold);
    gfloat gc = (gate_open) ? 1.0f : 0.0f;
    if (gate_open) {
      s->hold_attack_counter += period;
      s->hold_release_counter = 0.0;
      if (s->hold_attack_counter > attack_hold_time && gc > s->previous_gain) {
        gain = MAX((attack_coeff * s->previous_gain) + (1.0f - attack_coeff) * gc, 0.0f);
      } else if (s->hold_attack_counter <= attack_hold_time) {
        gain = s->previous_gain;
      }
    } else if (gate_close) {
      s->hold_attack_counter = 0.0;
      s->hold_release_counter += period;
      if (s->hold_release_counter > release_hold_time && gc <= s->previous_gain) {
        gain = MIN((release_coeff * s->previous_gain) + (1.0f - release_coeff) * gc, 1.0f);
      } else if (s->hold_release_counter <= release_hold_time) {
        gain = s->previous_gain;
      }
    } else {
      s->hold_attack_counter = 0.0;
      s->hold_release_counter = 0.0;
      gain = s->previous_gain;
    }

    s->previous_gain = gain;

    for (c = 0; c < in_channels; c++) {
      dst[c] = src[c] * gain * makeup;
    }
  }
}
