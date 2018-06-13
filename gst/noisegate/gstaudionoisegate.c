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

// A fake infinity value (because real infinity may break some hosts)
#define FAKE_INFINITY (65536.0 * 65536.0)

// Check for infinity (with appropriate-ish tolerance)
#define IS_FAKE_INFINITY(value) (fabs(value-FAKE_INFINITY) < 1.0)

enum
{
  PROP_0,
  PROP_LEVEL_IN,
  PROP_RANGE,
  PROP_THRESHOLD,
  PROP_RATIO,
  PROP_ATTACK,
  PROP_RELEASE,
  PROP_MAKEUP,
  PROP_KNEE,
  PROP_DETECTION,
  PROP_LINK,
  PROP_LEVEL_SC
};

enum
{
  DETECTION_PEAK = 0,
  DETECTION_RMS
};

#define GST_TYPE_AUDIO_NOISE_GATE_DETECTION (gst_audio_noise_gate_detection_get_type ())
static GType
gst_audio_noise_gate_detection_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {DETECTION_PEAK, "Peak",
          "peak"},
      {DETECTION_RMS, "RMS",
          "rms"},
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("GstAudioNoiseGateDetection", values);
  }
  return gtype;
}

enum
{
  LINK_AVERAGE = 0,
  LINK_MAXIMUM
};
#define GST_TYPE_AUDIO_NOISE_GATE_LINK (gst_audio_noise_gate_link_get_type ())
static GType
gst_audio_noise_gate_link_get_type (void)
{
  static GType gtype = 0;

  if (gtype == 0) {
    static const GEnumValue values[] = {
      {LINK_AVERAGE, "Average",
          "average"},
      {LINK_MAXIMUM, "Maximum",
          "maximum"},
      {0, NULL, NULL}
    };

    gtype = g_enum_register_static ("GstAudioNoiseGateLink", values);
  }
  return gtype;
}

static void gst_audio_noise_gate_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_audio_noise_gate_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_audio_noise_gate_setup (GstAudioFilter * filter,
    const GstAudioInfo * info);
static GstFlowReturn gst_audio_noise_gate_filter (GstBaseTransform * bt,
    GstBuffer * outbuf, GstBuffer * inbuf);
static GstFlowReturn
gst_audio_noise_gate_filter_inplace (GstBaseTransform * base_transform,
    GstBuffer * buf);

static gdouble output_gain(gdouble lin_slope, gdouble ratio, gdouble thres,
                          gdouble knee, gdouble knee_start, gdouble knee_stop,
                          gdouble lin_knee_stop, gdouble range);
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

#define DEFAULT_LEVEL_IN    1.0
#define DEFAULT_LEVEL_SC    1.0
#define DEFAULT_ATTACK      20.0
#define DEFAULT_RELEASE     250.0
// #define DEFAULT_THRESHOLD   0.125
#define DEFAULT_THRESHOLD   0.005
#define DEFAULT_RATIO       2.0
#define DEFAULT_KNEE        2.828427125
#define DEFAULT_MAKEUP      1.0
#define DEFAULT_RANGE       0.06125
#define DEFAULT_DETECTION   DETECTION_RMS
#define DEFAULT_LINK        LINK_AVERAGE

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

  g_object_class_install_property (gobject_class, PROP_LEVEL_IN,
      g_param_spec_double ("level-in", "Input gain",
          "Set input level before filtering", 0.015625, 64,
          DEFAULT_LEVEL_IN,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LEVEL_SC,
      g_param_spec_double ("level-sc", "Sidechain gain",
          "set sidechain gain", 0.015625, 64,
          DEFAULT_LEVEL_SC,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_ATTACK,
      g_param_spec_double ("attack", "Attack",
          "Amount of milliseconds the signal has to rise above the threshold before gain reduction stops", 
          0.01, 9000.0,
          DEFAULT_ATTACK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RELEASE,
      g_param_spec_double ("release", "Release",
          "Amount of milliseconds the signal has to fall below the threshold before the reduction is increased again",
          0.01, 9000.0,
          DEFAULT_RELEASE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_THRESHOLD,
      g_param_spec_double ("threshold", "Threshold",
          "If a signal rises above this level the gain reduction is released", 0.0, 1.0,
          DEFAULT_THRESHOLD,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RATIO,
      g_param_spec_double ("ratio", "Ratio",
          "Set a ratio by which the signal is reduced", 1.0, 9000.0,
          DEFAULT_RATIO,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_KNEE,
      g_param_spec_double ("knee", "Knee",
          "Curve the sharp knee around the threshold to enter gain reduction more softly", 1.0, 8.0,
          DEFAULT_KNEE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAKEUP,
      g_param_spec_double ("makeup", "Makeup",
          "Set amount of amplification of signal after processing", 1.0, 64.0,
          DEFAULT_MAKEUP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RANGE,
      g_param_spec_double ("range", "Range",
          "Set the level of gain reduction when the signal is below the threshold", 0.0, 1.0,
          DEFAULT_RANGE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_DETECTION,
      g_param_spec_enum ("detection", "Detection",
          "Choose if exact signal should be taken for detection or an RMS like one",
          GST_TYPE_AUDIO_NOISE_GATE_DETECTION, DEFAULT_DETECTION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LINK,
      g_param_spec_enum ("link", "Link",
          "Choose if the average level between all channels or the louder channel affects the reduction",
          GST_TYPE_AUDIO_NOISE_GATE_LINK, DEFAULT_LINK,
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
  filter->level_in = DEFAULT_LEVEL_IN;
  filter->level_sc = DEFAULT_LEVEL_SC;
  filter->attack = DEFAULT_ATTACK;
  filter->release = DEFAULT_RELEASE;
  filter->threshold = DEFAULT_THRESHOLD;
  filter->ratio = DEFAULT_RATIO;
  filter->knee = DEFAULT_KNEE;
  filter->makeup = DEFAULT_MAKEUP;
  filter->range = DEFAULT_RANGE;
  filter->link = DEFAULT_DETECTION;
  filter->detection = DEFAULT_LINK;

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
    case PROP_LEVEL_IN:
      filter->level_in = g_value_get_double (value);
      break;
    case PROP_RANGE:
      filter->range= g_value_get_double (value);
      break;
    case PROP_THRESHOLD:
      filter->threshold = g_value_get_double (value);
      break;
    case PROP_RATIO:
      filter->ratio = g_value_get_double (value);
      break;
    case PROP_ATTACK:
      filter->attack = g_value_get_double (value);
      break;
    case PROP_RELEASE:
      filter->release = g_value_get_double (value);
      break;
    case PROP_MAKEUP:
      filter->makeup = g_value_get_double (value);
      break;
    case PROP_KNEE:
      filter->knee = g_value_get_double (value);
      break;
    case PROP_DETECTION:
      filter->detection = g_value_get_enum (value);
      break;
    case PROP_LINK:
      filter->link = g_value_get_enum (value);
      break;
    case PROP_LEVEL_SC:
      filter->level_sc = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (filter);
}

static void
gst_audio_noise_gate_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAudioNoiseGate *filter = GST_AUDIO_NOISE_GATE (object);

  GST_OBJECT_LOCK (filter);
  switch (prop_id) {
    case PROP_LEVEL_IN:
      g_value_set_double(value, filter->level_in);
      break;
    case PROP_RANGE:
      g_value_set_double(value, filter->range);
      break;
    case PROP_THRESHOLD:
      g_value_set_double(value, filter->threshold);
      break;
    case PROP_RATIO:
      g_value_set_double(value, filter->ratio);
      break;
    case PROP_ATTACK:
      g_value_set_double(value, filter->attack);
      break;
    case PROP_RELEASE:
      g_value_set_double(value, filter->release);
      break;
    case PROP_MAKEUP:
      g_value_set_double(value, filter->makeup);
      break;
    case PROP_KNEE:
      g_value_set_double(value, filter->knee);
      break;
    case PROP_DETECTION:
      g_value_set_enum(value, filter->detection);
      break;
    case PROP_LINK:
      g_value_set_enum(value, filter->link);
      break;
    case PROP_LEVEL_SC:
      g_value_set_double(value, filter->level_sc);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (filter);
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

  GST_INFO_OBJECT (filter, "format %d (%s), rate %d, %d channels",
      fmt, GST_AUDIO_INFO_NAME (info), rate, chans);

  /* if any setup needs to be done (like memory allocated), do it here */

  /* The audio filter base class also saves the audio info in
   * GST_AUDIO_FILTER_INFO(filter) so it's automatically available
   * later from there as well */

  gdouble lin_threshold = filter->threshold;
  gdouble lin_knee_sqrt = sqrt(filter->knee);
  gdouble lin_knee_start;

  if (filter->detection)
    lin_threshold *= lin_threshold;

  filter->attack_coeff  = MIN(1.0, 1.0 / (filter->attack * rate / 4000.0));
  filter->release_coeff = MIN(1.0, 1.0 / (filter->release * rate / 4000.0));
  filter->lin_knee_stop = lin_threshold * lin_knee_sqrt;
  lin_knee_start = lin_threshold / lin_knee_sqrt;
  filter->thres = log(lin_threshold);
  filter->knee_start = log(lin_knee_start);
  filter->knee_stop = log(filter->lin_knee_stop);

  filter->process = (fmt == GST_AUDIO_FORMAT_F32) ?
    (GstAudioNoiseGateProcessFunc) gate_float :
    (GstAudioNoiseGateProcessFunc) gate_int16;

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

  GST_LOG_OBJECT (filter, "transform buffer");

  /* FIXME: do something interesting here.  We simply copy the input data
   * to the output buffer for now. */
  if (gst_buffer_map (inbuf, &map_in, GST_MAP_READ)) {
    if (gst_buffer_map (outbuf, &map_out, GST_MAP_WRITE)) {
      g_assert (map_out.size == map_in.size);

      // memcpy (map_out.data, map_in.data, map_out.size);
      gint nbsamples = map_in.size / GST_AUDIO_INFO_BPF(info);
      filter->process(filter,
          map_in.data, map_out.data, map_in.data,
          nbsamples, filter->level_in, filter->level_sc);

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

  GST_LOG_OBJECT (filter, "transform buffer in place");

  /* FIXME: do something interesting here.  Doing nothing means the input
   * buffer is simply pushed out as is without any modification */
  if (gst_buffer_map (buf, &map, GST_MAP_READWRITE)) {
#if 0
    switch (GST_AUDIO_FILTER_FORMAT (filter)) {
      case GST_AUDIO_FORMAT_S16LE:
      case GST_AUDIO_FORMAT_S16BE: {
        gint16 *samples = map.data;
        guint n_samples = map.size / sizeof (gint16);
        guint i;

        for (i = 0; i < n; ++n) {
          samples[i] = samples[i];
        }
        break;
      }
      default:
        g_warning ("Unexpected audio format %s!",
            GST_AUDIO_INFO_NAME (GST_AUDIO_FILTER_INFO(filter)));
        flow = GST_FLOW_ERROR;
        break;
    }
#endif
    gst_buffer_unmap (buf, &map);
  }

  return flow;
}

static inline gdouble hermite_interpolation(
    gdouble x, gdouble x0, gdouble x1,
    gdouble p0, gdouble p1,
    gdouble m0, gdouble m1)
{
  gdouble width = x1 - x0;
  gdouble t = (x - x0) / width;
  gdouble t2, t3;
  gdouble ct0, ct1, ct2, ct3;
  m0 *= width;
  m1 *= width;

  t2 = t*t;
  t3 = t2*t;
  ct0 = p0;
  ct1 = m0;

  ct2 = -3 * p0 - 2 * m0 + 3 * p1 - m1;
  ct3 = 2 * p0 + m0  - 2 * p1 + m1;

  return ct3 * t3 + ct2 * t2 + ct1 * t + ct0;
}

static gdouble output_gain(
    gdouble lin_slope, gdouble ratio, gdouble thres,
    gdouble knee, gdouble knee_start, gdouble knee_stop,
    gdouble lin_knee_stop, gdouble range)
{
  if (lin_slope < lin_knee_stop) {
    gdouble slope = log(lin_slope);
    gdouble tratio = ratio;
    gdouble gain = 0.0;
    gdouble delta = 0.0;

    if (IS_FAKE_INFINITY(ratio))
      tratio = 1000.0;
    gain = (slope - thres) * tratio + thres;
    delta = tratio;

    if (knee > 1.0 && slope > knee_start) {
      gain = hermite_interpolation(slope, knee_start, knee_stop,
          ((knee_start - thres) * tratio  + thres), knee_stop, 
          delta, 1.0);
    }
    return MAX(range, exp(gain - slope));
  }

  return 1.0;
}

static void gate_float(GstAudioNoiseGate *s,
    const gfloat *src, gfloat *dst, const gfloat *scsrc,
    int nb_samples, double level_in, double level_sc)
{
  const GstAudioInfo* info = GST_AUDIO_FILTER_INFO(s);
  const gdouble makeup = s->makeup;
  const gdouble attack_coeff = s->attack_coeff;
  const gdouble release_coeff = s->release_coeff;
  int n, c;
  int in_channels, out_channels;

  in_channels = out_channels = GST_AUDIO_INFO_CHANNELS(info);

  for (n = 0; n < nb_samples; n++, src += in_channels, dst += in_channels, scsrc += out_channels) {
    gdouble abs_sample = fabs(scsrc[0] * level_sc), gain = 1.0;

    if (s->link == 1) {
      for (c = 1; c < out_channels; c++)
        abs_sample = MAX(fabs(scsrc[c] * level_sc), abs_sample);
    } else {
      for (c = 1; c < out_channels; c++)
        abs_sample += fabs(scsrc[c] * level_sc);

      abs_sample /= out_channels;
    }

    if (s->detection)
      abs_sample *= abs_sample;

    s->lin_slope += (abs_sample - s->lin_slope) * (abs_sample > s->lin_slope ? attack_coeff : release_coeff);
    if (s->lin_slope > 0.0)
      gain = output_gain(s->lin_slope, s->ratio, s->thres,
          s->knee, s->knee_start, s->knee_stop,
          s->lin_knee_stop, s->range);

    for (c = 0; c < in_channels; c++)
      dst[c] = src[c] * level_in * gain * makeup;
  }
}

static void gate_int16(GstAudioNoiseGate *s,
    const gint16 *src, gint16 *dst, const gint16 *scsrc,
    int nb_samples, double level_in, double level_sc)
{
  const GstAudioInfo* info = GST_AUDIO_FILTER_INFO(s);
  const gdouble makeup = s->makeup;
  const gdouble attack_coeff = s->attack_coeff;
  const gdouble release_coeff = s->release_coeff;
  int n, c;
  int in_channels, out_channels;

  in_channels = out_channels = GST_AUDIO_INFO_CHANNELS(info);

  for (n = 0; n < nb_samples; n++, src += in_channels, dst += in_channels, scsrc += out_channels) {
    gdouble abs_sample = fabs(scsrc[0] * level_sc), gain = 1.0;

    if (s->link == 1) {
      for (c = 1; c < out_channels; c++)
        abs_sample = MAX(fabs(scsrc[c] * level_sc), abs_sample);
    } else {
      for (c = 1; c < out_channels; c++)
        abs_sample += fabs(scsrc[c] * level_sc);

      abs_sample /= out_channels;
    }

    if (s->detection)
      abs_sample *= abs_sample;

    s->lin_slope += (abs_sample - s->lin_slope) * (abs_sample > s->lin_slope ? attack_coeff : release_coeff);
    if (s->lin_slope > 0.0)
      gain = output_gain(s->lin_slope, s->ratio, s->thres,
          s->knee, s->knee_start, s->knee_stop,
          s->lin_knee_stop, s->range);

    for (c = 0; c < in_channels; c++)
      dst[c] = src[c] * level_in * gain * makeup;
  }
}

