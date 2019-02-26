/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dxgicontext.h"

#include <gmodule.h>
#include <string.h>

G_DEFINE_TYPE (GstDXGIContext, gst_dxgi_context, GST_TYPE_DXGI_CONTEXT);

#define GST_CAT_DEFAULT gst_dxgi_context_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);
GST_DEBUG_CATEGORY_STATIC (gst_dxgi_debug);


static void gst_dxgi_context_finalize (GObject * object);

static void
_init_debug (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_dxgi_context_debug, "dxgicontext", 0,
        "dxgicontext element");
    GST_DEBUG_CATEGORY_INIT (gst_dxgi_debug, "dxgidebug", 0, "DXGI Debugging");
    g_once_init_leave (&_init, 1);
  }
}

static void
gst_dxgi_context_class_init (GstDXGIContextClass * klass)
{
  // klass->get_proc_address =
  //  GST_DEBUG_FUNCPTR (gst_dxgi_context_default_get_proc_address);

  G_OBJECT_CLASS (klass)->finalize = gst_dxgi_context_finalize;

  _init_debug ();
}

static void
gst_dxgi_context_init (GstDXGIContext * context)
{
  context->display = NULL;
}

static void
gst_dxgi_context_finalize (GObject * object)
{
  GstDXGIContext *context = GST_DXGI_CONTEXT (object);

  GST_DEBUG_OBJECT (context, "End of finalize");
  G_OBJECT_CLASS (gst_dxgi_context_parent_class)->finalize (object);
}

GstDXGIContext *
gst_dxgi_context_new (GstDXGIDisplay * display)
{
  GstDXGIContext *context = NULL;

  _init_debug ();

  // context = gst_dxgi_context_dx12_new (display);

  if (!context) {
    return NULL;
  }

  context->display = gst_object_ref (display);

  GST_DEBUG_OBJECT (context,
      "Done creating context for display %" GST_PTR_FORMAT ".",
      display);

  return context;
}


