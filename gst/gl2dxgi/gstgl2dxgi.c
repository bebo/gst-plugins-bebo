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
static gboolean gst_gl_2_dxgi_decide_allocation (GstBaseTransform * 
     trans, GstQuery * query); 
/*static GstFlowReturn */
gst_gl_2_dxgi_prepare_output_buffer (GstBaseTransform * bt, 
    GstBuffer * buffer, GstBuffer ** outbuf);
static GstFlowReturn gst_gl_2_dxgi_transform (GstBaseTransform * bt,
    GstBuffer * buffer, GstBuffer * outbuf);
static gboolean gst_gl_2_dxgi_stop (GstBaseTransform * bt);
static gboolean gst_gl_2_dxgi_start (GstBaseTransform * bt);
static void gst_gl_2_dxgi_set_context (GstElement * element,
    GstContext * context);


static GstStaticPadTemplate gst_gl_2_dxgi_src_pad_template =
GST_STATIC_PAD_TEMPLATE("src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS("video/x-raw(" GST_CAPS_FEATURE_MEMORY_GL_MEMORY "), "
    "format = (string) RGBA, "
    "width = (int) [ 16, 4096 ], "
    "height = (int) [ 16, 2160 ], "
    "framerate = (fraction) [0, 120], "
    "texture-target = (string) 2D"
  ));

static GstStaticPadTemplate gst_gl_2_dxgi_sink_pad_template =
GST_STATIC_PAD_TEMPLATE("sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS("video/x-raw(" GST_CAPS_FEATURE_MEMORY_GL_MEMORY "), "
    "format = (string) RGBA, "
    "width = (int) [ 16, 4096 ], "
    "height = (int) [ 16, 2160 ], "
    "framerate = (fraction) [0, 120], "
    "texture-target = (string) 2D"
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
static GstCaps *
gst_gl_2_dxgi_fixate_caps(GstBaseTransform * bt,
  GstPadDirection direction, GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;
  GValue fpar = { 0, }, tpar = {
    0, };

  othercaps = gst_caps_make_writable(othercaps);
  othercaps = gst_caps_truncate(othercaps);

  GST_DEBUG_OBJECT(bt, "trying to fixate othercaps %" GST_PTR_FORMAT
    " based on caps %" GST_PTR_FORMAT, othercaps, caps);

  ins = gst_caps_get_structure(caps, 0);
  outs = gst_caps_get_structure(othercaps, 0);

  from_par = gst_structure_get_value(ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value(outs, "pixel-aspect-ratio");

  /* If we're fixating from the sinkpad we always set the PAR and
  * assume that missing PAR on the sinkpad means 1/1 and
  * missing PAR on the srcpad means undefined
  */
  if (direction == GST_PAD_SINK) {
    if (!from_par) {
      g_value_init(&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction(&fpar, 1, 1);
      from_par = &fpar;
    }
    if (!to_par) {
      g_value_init(&tpar, GST_TYPE_FRACTION);
      gst_value_set_fraction(&tpar, 1, 1);
      to_par = &tpar;
    }
  }
  else {
    if (!to_par) {
      g_value_init(&tpar, GST_TYPE_FRACTION);
      gst_value_set_fraction(&tpar, 1, 1);
      to_par = &tpar;

      gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1,
        NULL);
    }
    if (!from_par) {
      g_value_init(&fpar, GST_TYPE_FRACTION);
      gst_value_set_fraction(&fpar, 1, 1);
      from_par = &fpar;
    }
  }

  /* we have both PAR but they might not be fixated */
  {
    gint from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
    gint w = 0, h = 0;
    gint from_dar_n, from_dar_d;
    gint num, den;

    /* from_par should be fixed */
    g_return_val_if_fail(gst_value_is_fixed(from_par), othercaps);

    from_par_n = gst_value_get_fraction_numerator(from_par);
    from_par_d = gst_value_get_fraction_denominator(from_par);

    gst_structure_get_int(ins, "width", &from_w);
    gst_structure_get_int(ins, "height", &from_h);

    gst_structure_get_int(outs, "width", &w);
    gst_structure_get_int(outs, "height", &h);

    /* if both width and height are already fixed, we can't do anything
    * about it anymore */
    if (w && h) {
      GST_DEBUG_OBJECT(bt, "dimensions already set to %dx%d, not fixating",
        w, h);
      if (!gst_value_is_fixed(to_par)) {
        GST_DEBUG_OBJECT(bt, "fixating to_par to %dx%d", 1, 1);
        if (gst_structure_has_field(outs, "pixel-aspect-ratio"))
          gst_structure_fixate_field_nearest_fraction(outs,
            "pixel-aspect-ratio", 1, 1);
      }
      goto done;
    }

    /* Calculate input DAR */
    if (!gst_util_fraction_multiply(from_w, from_h, from_par_n, from_par_d,
      &from_dar_n, &from_dar_d)) {
      GST_ELEMENT_ERROR(bt, CORE, NEGOTIATION, (NULL),
        ("Error calculating the output scaled size - integer overflow"));
      goto done;
    }

    GST_DEBUG_OBJECT(bt, "Input DAR is %d/%d", from_dar_n, from_dar_d);

    /* If either width or height are fixed there's not much we
    * can do either except choosing a height or width and PAR
    * that matches the DAR as good as possible
    */
    if (h) {
      gint num, den;

      GST_DEBUG_OBJECT(bt, "height is fixed (%d)", h);

      if (!gst_value_is_fixed(to_par)) {
        /* (shortcut) copy-paste (??) of videoscale seems to aim for 1/1,
        * so let's make it so ...
        * especially if following code assumes fixed */
        GST_DEBUG_OBJECT(bt, "fixating to_par to 1x1");
        gst_structure_fixate_field_nearest_fraction(outs,
          "pixel-aspect-ratio", 1, 1);
        to_par = gst_structure_get_value(outs, "pixel-aspect-ratio");
      }

      /* PAR is fixed, choose the height that is nearest to the
      * height with the same DAR */
      to_par_n = gst_value_get_fraction_numerator(to_par);
      to_par_d = gst_value_get_fraction_denominator(to_par);

      GST_DEBUG_OBJECT(bt, "PAR is fixed %d/%d", to_par_n, to_par_d);

      if (!gst_util_fraction_multiply(from_dar_n, from_dar_d, to_par_d,
        to_par_n, &num, &den)) {
        GST_ELEMENT_ERROR(bt, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint)gst_util_uint64_scale_int(h, num, den);
      gst_structure_fixate_field_nearest_int(outs, "width", w);

      goto done;
    }
    else if (w) {
      gint num, den;

      GST_DEBUG_OBJECT(bt, "width is fixed (%d)", w);

      if (!gst_value_is_fixed(to_par)) {
        /* (shortcut) copy-paste (??) of videoscale seems to aim for 1/1,
        * so let's make it so ...
        * especially if following code assumes fixed */
        GST_DEBUG_OBJECT(bt, "fixating to_par to 1x1");
        gst_structure_fixate_field_nearest_fraction(outs,
          "pixel-aspect-ratio", 1, 1);
        to_par = gst_structure_get_value(outs, "pixel-aspect-ratio");
      }

      /* PAR is fixed, choose the height that is nearest to the
      * height with the same DAR */
      to_par_n = gst_value_get_fraction_numerator(to_par);
      to_par_d = gst_value_get_fraction_denominator(to_par);

      GST_DEBUG_OBJECT(bt, "PAR is fixed %d/%d", to_par_n, to_par_d);

      if (!gst_util_fraction_multiply(from_dar_n, from_dar_d, to_par_d,
        to_par_n, &num, &den)) {
        GST_ELEMENT_ERROR(bt, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      h = (guint)gst_util_uint64_scale_int(w, den, num);
      gst_structure_fixate_field_nearest_int(outs, "height", h);

      goto done;
    }
    else if (gst_value_is_fixed(to_par)) {
      GstStructure *tmp;
      gint set_h, set_w, f_h, f_w;

      to_par_n = gst_value_get_fraction_numerator(to_par);
      to_par_d = gst_value_get_fraction_denominator(to_par);

      /* Calculate scale factor for the PAR change */
      if (!gst_util_fraction_multiply(from_dar_n, from_dar_d, to_par_n,
        to_par_d, &num, &den)) {
        GST_ELEMENT_ERROR(bt, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      /* Try to keep the input height */
      tmp = gst_structure_copy(outs);
      gst_structure_fixate_field_nearest_int(tmp, "height", from_h);
      gst_structure_get_int(tmp, "height", &set_h);

      /* This might have failed but try to scale the width
      * to keep the DAR nonetheless */
      w = (guint)gst_util_uint64_scale_int(set_h, num, den);
      gst_structure_fixate_field_nearest_int(tmp, "width", w);
      gst_structure_get_int(tmp, "width", &set_w);
      gst_structure_free(tmp);

      /* We kept the DAR and the height is nearest to the original height */
      if (set_w == w) {
        gst_structure_set(outs, "width", G_TYPE_INT, set_w, "height",
          G_TYPE_INT, set_h, NULL);
        goto done;
      }

      f_h = set_h;
      f_w = set_w;

      /* If the former failed, try to keep the input width at least */
      tmp = gst_structure_copy(outs);
      gst_structure_fixate_field_nearest_int(tmp, "width", from_w);
      gst_structure_get_int(tmp, "width", &set_w);

      /* This might have failed but try to scale the width
      * to keep the DAR nonetheless */
      h = (guint)gst_util_uint64_scale_int(set_w, den, num);
      gst_structure_fixate_field_nearest_int(tmp, "height", h);
      gst_structure_get_int(tmp, "height", &set_h);
      gst_structure_free(tmp);

      /* We kept the DAR and the width is nearest to the original width */
      if (set_h == h) {
        gst_structure_set(outs, "width", G_TYPE_INT, set_w, "height",
          G_TYPE_INT, set_h, NULL);
        goto done;
      }

      /* If all this failed, keep the height that was nearest to the orignal
      * height and the nearest possible width. This changes the DAR but
      * there's not much else to do here.
      */
      gst_structure_set(outs, "width", G_TYPE_INT, f_w, "height", G_TYPE_INT,
        f_h, NULL);
      goto done;
    }
    else {
      GstStructure *tmp;
      gint set_h, set_w, set_par_n, set_par_d, tmp2;

      /* width, height and PAR are not fixed */

      /* First try to keep the height and width as good as possible
      * and scale PAR */
      tmp = gst_structure_copy(outs);
      gst_structure_fixate_field_nearest_int(tmp, "height", from_h);
      gst_structure_get_int(tmp, "height", &set_h);
      gst_structure_fixate_field_nearest_int(tmp, "width", from_w);
      gst_structure_get_int(tmp, "width", &set_w);

      if (!gst_util_fraction_multiply(from_dar_n, from_dar_d, set_h, set_w,
        &to_par_n, &to_par_d)) {
        GST_ELEMENT_ERROR(bt, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
        gst_structure_free(tmp);
        goto done;
      }

      if (!gst_structure_has_field(tmp, "pixel-aspect-ratio"))
        gst_structure_set_value(tmp, "pixel-aspect-ratio", to_par);
      gst_structure_fixate_field_nearest_fraction(tmp, "pixel-aspect-ratio",
        to_par_n, to_par_d);
      gst_structure_get_fraction(tmp, "pixel-aspect-ratio", &set_par_n,
        &set_par_d);
      gst_structure_free(tmp);

      if (set_par_n == to_par_n && set_par_d == to_par_d) {
        gst_structure_set(outs, "width", G_TYPE_INT, set_w, "height",
          G_TYPE_INT, set_h, NULL);

        if (gst_structure_has_field(outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
          gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);
        goto done;
      }

      /* Otherwise try to scale width to keep the DAR with the set
      * PAR and height */
      if (!gst_util_fraction_multiply(from_dar_n, from_dar_d, set_par_d,
        set_par_n, &num, &den)) {
        GST_ELEMENT_ERROR(bt, CORE, NEGOTIATION, (NULL),
          ("Error calculating the output scaled size - integer overflow"));
        goto done;
      }

      w = (guint)gst_util_uint64_scale_int(set_h, num, den);
      tmp = gst_structure_copy(outs);
      gst_structure_fixate_field_nearest_int(tmp, "width", w);
      gst_structure_get_int(tmp, "width", &tmp2);
      gst_structure_free(tmp);

      if (tmp2 == w) {
        gst_structure_set(outs, "width", G_TYPE_INT, tmp2, "height",
          G_TYPE_INT, set_h, NULL);
        if (gst_structure_has_field(outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
          gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);
        goto done;
      }

      /* ... or try the same with the height */
      h = (guint)gst_util_uint64_scale_int(set_w, den, num);
      tmp = gst_structure_copy(outs);
      gst_structure_fixate_field_nearest_int(tmp, "height", h);
      gst_structure_get_int(tmp, "height", &tmp2);
      gst_structure_free(tmp);

      if (tmp2 == h) {
        gst_structure_set(outs, "width", G_TYPE_INT, set_w, "height",
          G_TYPE_INT, tmp2, NULL);
        if (gst_structure_has_field(outs, "pixel-aspect-ratio") ||
          set_par_n != set_par_d)
          gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
            set_par_n, set_par_d, NULL);
        goto done;
      }

      /* If all fails we can't keep the DAR and take the nearest values
      * for everything from the first try */
      gst_structure_set(outs, "width", G_TYPE_INT, set_w, "height",
        G_TYPE_INT, set_h, NULL);
      if (gst_structure_has_field(outs, "pixel-aspect-ratio") ||
        set_par_n != set_par_d)
        gst_structure_set(outs, "pixel-aspect-ratio", GST_TYPE_FRACTION,
          set_par_n, set_par_d, NULL);
    }
  }


done:
  othercaps = gst_caps_fixate(othercaps);

  GST_DEBUG_OBJECT(bt, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);

  if (from_par == &fpar)
    g_value_unset(&fpar);
  if (to_par == &tpar)
    g_value_unset(&tpar);

  return othercaps;
}

/* copies the given caps */
static GstCaps *
gst_gl_2_dxgi_caps_remove_size(GstCaps * caps)
{
  GstStructure *st;
  GstCapsFeatures *f;
  gint i, n;
  GstCaps *res;

  res = gst_caps_new_empty();

  n = gst_caps_get_size(caps);
  for (i = 0; i < n; i++) {
    st = gst_caps_get_structure(caps, i);
    f = gst_caps_get_features(caps, i);

    /* If this is already expressed by the existing caps
    * skip this structure */
    if (i > 0 && gst_caps_is_subset_structure_full(res, st, f))
      continue;

    st = gst_structure_copy(st);
    gst_structure_set(st, "width", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "height", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);

    /* if pixel aspect ratio, make a range of it */
    if (gst_structure_has_field(st, "pixel-aspect-ratio")) {
      gst_structure_set(st, "pixel-aspect-ratio",
        GST_TYPE_FRACTION_RANGE, 1, G_MAXINT, G_MAXINT, 1, NULL);
    }

    gst_caps_append_structure_full(res, st, gst_caps_features_copy(f));
  }

  return res;
}

static GstCaps *
gst_gl_2_dxgi_set_caps_features(const GstCaps * caps,
  const gchar * feature_name)
{
  GstCaps *ret = gst_caps_copy(caps);
  guint n = gst_caps_get_size(ret);
  guint i = 0;

  for (i = 0; i < n; i++) {
    gst_caps_set_features(ret, i,
      gst_caps_features_from_string(GST_CAPS_FEATURE_MEMORY_GL_MEMORY));
  }

  return ret;
}


static GstCaps *
gst_gl_2_dxgi_transform_caps(GstBaseTransform * bt,
  GstPadDirection direction, GstCaps * caps, GstCaps * filter_caps)
{
  GstGL2DXGI *self = GST_GL_2_DXGI(bt);

  GstCaps *tmp = gst_caps_ref(caps);

  GstCaps *result = 
      gst_gl_2_dxgi_set_caps_features(tmp,
        GST_CAPS_FEATURE_MEMORY_GL_MEMORY);
    gst_caps_unref(tmp);

  GST_DEBUG_OBJECT(bt, "returning caps: %" GST_PTR_FORMAT, result);

  return result;
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

static gboolean
gst_gl_2_dxgi_gl_set_caps(GstGLBaseFilter * bt, GstCaps * incaps,
  GstCaps * outcaps)
{
  //GstGL2DXGI *self = GST_GL_2_DXGI(bt);
  //GstGL2DXGIClass *gl2dxgi_class = GST_GL_2_DXGI_GET_CLASS(self);
  //GstGLContext *context = GST_GL_BASE_FILTER(self)->context;
  return TRUE;
}

static gboolean
gst_gl_2_dxgi_set_caps(GstBaseTransform * bt, GstCaps * incaps,
  GstCaps * outcaps)
{
  //GstGLFilter *filter;
  //GstGLFilterClass *filter_class;
  GstGLTextureTarget from_target, to_target;

  GstGL2DXGI *self = GST_GL_2_DXGI(bt);
  GstGL2DXGIClass *gl2dxgi_class = GST_GL_2_DXGI_GET_CLASS(self);
  //filter = GST_GL_FILTER(bt);
  //filter_class = GST_GL_FILTER_GET_CLASS(filter);

  if (!gst_video_info_from_caps(&self->in_info, incaps))
    goto wrong_caps;
  if (!gst_video_info_from_caps(&self->out_info, outcaps))
    goto wrong_caps;

  {
    GstStructure *in_s = gst_caps_get_structure(incaps, 0);
    GstStructure *out_s = gst_caps_get_structure(outcaps, 0);

    if (gst_structure_has_field_typed(in_s, "texture-target", G_TYPE_STRING))
      from_target =
      gst_gl_texture_target_from_string(gst_structure_get_string(in_s,
        "texture-target"));
    else
      from_target = GST_GL_TEXTURE_TARGET_2D;

    if (gst_structure_has_field_typed(out_s, "texture-target", G_TYPE_STRING))
      to_target =
      gst_gl_texture_target_from_string(gst_structure_get_string(out_s,
        "texture-target"));
    else
      to_target = GST_GL_TEXTURE_TARGET_2D;

    if (to_target == GST_GL_TEXTURE_TARGET_NONE
      || from_target == GST_GL_TEXTURE_TARGET_NONE)
      /* invalid caps */
      goto wrong_caps;
  }


#if 0 // FIXME
  if (gl2dxgi_class->set_caps) {
    if (!gl2dxgi_class->set_caps(self, incaps, outcaps))
      goto error;
  }
#endif

  gst_caps_replace(&self->out_caps, outcaps);
  self->in_texture_target = from_target;
  self->out_texture_target = to_target;

  GST_DEBUG_OBJECT(self, "set_caps %dx%d in %" GST_PTR_FORMAT
    " out %" GST_PTR_FORMAT,
    GST_VIDEO_INFO_WIDTH(&self->out_info),
    GST_VIDEO_INFO_HEIGHT(&self->out_info), incaps, outcaps);

  return GST_BASE_TRANSFORM_CLASS(parent_class)->set_caps(bt, incaps,
    outcaps);

  /* ERRORS */
wrong_caps:
  {
    GST_WARNING("Wrong caps - could not understand input or output caps");
    return FALSE;
  }
error:
  {
    return FALSE;
  }
}

static void
gst_gl_2_dxgi_class_init (GstGL2DXGIClass * klass)
{
  GstBaseTransformClass *bt_class = GST_BASE_TRANSFORM_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstGLBaseFilterClass *gl_class = GST_GL_BASE_FILTER_CLASS(klass);

  bt_class->filter_meta = gst_gl_2_dxgi_filter_meta;
  bt_class->get_unit_size = gst_gl_2_dxgi_get_unit_size;
  bt_class->prepare_output_buffer = gst_gl_2_dxgi_prepare_output_buffer;
  bt_class->transform = gst_gl_2_dxgi_transform;
  bt_class->stop = gst_gl_2_dxgi_stop;
  bt_class->start = gst_gl_2_dxgi_start;

  bt_class->transform_caps = gst_gl_2_dxgi_transform_caps;
  bt_class->fixate_caps = gst_gl_2_dxgi_fixate_caps;
  bt_class->set_caps = gst_gl_2_dxgi_set_caps;
  bt_class->propose_allocation = gst_gl_2_dxgi_propose_allocation;
  bt_class->decide_allocation = gst_gl_2_dxgi_decide_allocation;

  bt_class->passthrough_on_same_caps = FALSE; // FIXME - should I touch this?
  klass->set_caps = NULL;
  gl_class->gl_set_caps = gst_gl_2_dxgi_gl_set_caps;
  //FIXME:
  //gl_class->gl_stop = gst_gl_filter_gl_stop;

  //FIXME
  //klass->transform_internal_caps = default_transform_internal_caps;


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
gst_gl_2_dxgi_init (GstGL2DXGI * self)
{
  gst_base_transform_set_prefer_passthrough (GST_BASE_TRANSFORM (self), TRUE);
  self->queue = g_async_queue_new();
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
gst_gl_2_dxgi_decide_allocation (GstBaseTransform * trans,
    GstQuery * query)
{
#if 0
  GstGLContext *context;
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;
  guint min, max, size;
  gboolean update_pool;

  gst_query_parse_allocation(query, &caps, NULL);
  if (!caps)
    return FALSE;

  /* get gl context */
  if (!GST_BASE_TRANSFORM_CLASS(parent_class)->decide_allocation(trans,
    query))
    return FALSE;

  context = GST_GL_BASE_FILTER(trans)->context;

  if (gst_query_get_n_allocation_pools(query) > 0) {
    gst_query_parse_nth_allocation_pool(query, 0, &pool, &size, &min, &max);

    update_pool = TRUE;
  } else {
    GstVideoInfo vinfo;

    gst_video_info_init(&vinfo);
    gst_video_info_from_caps(&vinfo, caps);
    size = vinfo.size;
    min = max = 0;
    update_pool = FALSE;
  }

  if (!pool || !GST_IS_GL_BUFFER_POOL(pool)) {
    if (pool)
      gst_object_unref(pool);
    pool = gst_gl_buffer_pool_new(context);
  }

  config = gst_buffer_pool_get_config(pool);

  gst_buffer_pool_config_set_params(config, caps, size, min, max);
  gst_buffer_pool_config_add_option(config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  if (gst_query_find_allocation_meta(query, GST_GL_SYNC_META_API_TYPE, NULL))
    gst_buffer_pool_config_add_option(config,
      GST_BUFFER_POOL_OPTION_GL_SYNC_META);

  gst_buffer_pool_set_config(pool, config);

  if (update_pool)
    gst_query_set_nth_allocation_pool(query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool(query, pool, size, min, max);

  gst_object_unref(pool);

  return TRUE;
#endif
  GstGL2DXGI *self = GST_GL_2_DXGI(trans);
  GST_ERROR_OBJECT(self, "gst_decide");
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


/* static gboolean */
/* _gst_gl_2_dxgi_set_caps (GstBaseTransform * bt, GstCaps * in_caps, */
/*     GstCaps * out_caps) */
/* { */
/*   GstGL2DXGI *upload = GST_GL_2_DXGI (bt); */

/*   return gst_gl_upload_set_caps (upload->upload, in_caps, out_caps); */
/* } */

GstFlowReturn 
gst_gl_2_dxgi_prepare_output_buffer(GstBaseTransform * bt,
  GstBuffer * buffer, GstBuffer ** outbuf)
{

#if 0
  GstGL2DXGI *self = GST_GL_2_DXGI(bt);
  g_async_queue_push(self->queue, buffer);
  gst_buffer_ref(buffer);

  if (g_async_queue_length(self->queue) < 5) {
    GstBuffer * buf = g_async_queue_try_pop(self->queue);
    g_async_queue_push_front(self->queue, buf);
    gst_buffer_ref(buf);
    GST_LOG("refcnt %d", buf->mini_object.refcount);
    *outbuf = buf;
    return GST_FLOW_OK;
  }

  GstBuffer * buf = g_async_queue_try_pop(self->queue);
  gst_buffer_unref(buf);
  GST_LOG("refcnt %d", buf->mini_object.refcount);
  *outbuf = buf;
#else
  *outbuf = buffer;
#endif
  return GST_FLOW_OK;
}
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
