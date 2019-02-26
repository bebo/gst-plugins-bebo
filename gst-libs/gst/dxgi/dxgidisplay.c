/*
 * Copyright (c) 2019 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dxgidisplay.h"

#include <gst/dxgi/d3d12/dxgidisplay_d3d12.h>

GST_DEBUG_CATEGORY_STATIC (gst_context);
GST_DEBUG_CATEGORY_STATIC (gst_dxgi_display_debug);
#define GST_CAT_DEFAULT gst_dxgi_display_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_dxgi_display_debug, "dxgidisplay", 0, "dxgi display"); \
  GST_DEBUG_CATEGORY_GET (gst_context, "GST_CONTEXT");

G_DEFINE_TYPE_WITH_CODE (GstDXGIDisplay, gst_dxgi_display, GST_TYPE_OBJECT,
    DEBUG_INIT);


static void gst_dxgi_display_dispose (GObject * object);
static void gst_dxgi_display_finalize (GObject * object);

static void
gst_dxgi_display_class_init (GstDXGIDisplayClass * klass)
{
  G_OBJECT_CLASS (klass)->finalize = gst_dxgi_display_finalize;
  G_OBJECT_CLASS (klass)->dispose = gst_dxgi_display_dispose;
}

static void
gst_dxgi_display_init (GstDXGIDisplay * display)
{
  display->type = GST_DXGI_DISPLAY_TYPE_ANY;

#if 0
  gst_dxgi_buffer_init_once ();
  gst_dxgi_memory_pbo_init_once ();
  gst_dxgi_renderbuffer_init_once ();
#endif
}

static void
gst_dxgi_display_dispose (GObject * object)
{
  GstDXGIDisplay *display = GST_DXGI_DISPLAY (object);

  G_OBJECT_CLASS (gst_dxgi_display_parent_class)->dispose (object);
}

static void
gst_dxgi_display_finalize (GObject * object)
{
  GstDXGIDisplay *display = GST_DXGI_DISPLAY (object);

  G_OBJECT_CLASS (gst_dxgi_display_parent_class)->finalize (object);
}

GstDXGIDisplay *
gst_dxgi_display_new (void)
{
  GstDXGIDisplay *display = NULL;
  const gchar *user_choice, *platform_choice;
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_dxgi_display_debug, "dxgidisplay", 0,
        "dxgidisplay element");
    g_once_init_leave (&_init, 1);
  }

  display = GST_DXGI_DISPLAY (gst_dxgi_display_d3d12_new ());

  if (!display) {
    GST_INFO ("Could not create dxgi display. user specified %s "
        "(platform: %s), creating dummy",
        GST_STR_NULL (user_choice), GST_STR_NULL (platform_choice));

    display = g_object_new (GST_TYPE_DXGI_DISPLAY, NULL);
    gst_object_ref_sink (display);
  }

  return display;
}

