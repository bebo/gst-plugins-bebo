/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdxgidevice_interop.h"
#include "gstdxgidevice_d3d12.h"

GST_DEBUG_CATEGORY_STATIC (gst_dxgi_device_interop_debug);
#define GST_CAT_DEFAULT   gst_dxgi_device_interop_debug

G_DEFINE_TYPE (GstDXGIDeviceInterop, gst_dxgi_device_interop,
    GST_TYPE_OBJECT);

/* prototypes */
static void gst_dxgi_device_interop_finalize (GObject * object);
static GstWGLFunctions* wgl_functions_new (GstGLContext * gl_context);

void
gst_dxgi_device_interop_init_once (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_dxgi_device_interop_debug, "dxgideviceinterop", 0,
        "dxgidevice interop element");

    g_once_init_leave (&_init, 1);
  }
}

static void
gst_dxgi_device_interop_class_init (GstDXGIDeviceInteropClass * klass)
{
  G_OBJECT_CLASS (klass)->finalize = gst_dxgi_device_interop_finalize;
}

static void
gst_dxgi_device_interop_init (GstDXGIDeviceInterop * device)
{
}

static void
gst_dxgi_device_interop_finalize (GObject * object)
{
  GstDXGIDeviceInterop *self = GST_DXGI_DEVICE_INTEROP (object);

  if (self->wgl_funcs) {
    g_free (self->wgl_funcs);
    self->wgl_funcs = NULL;
  }

  if (self->dxgi_device) {
    gst_object_unref (self->dxgi_device);
    self->dxgi_device = NULL;
  }

  G_OBJECT_CLASS (gst_dxgi_device_interop_parent_class)->finalize (object);
}

GstDXGIDeviceInterop *
gst_dxgi_device_interop_new_wrapped (GstGLContext * gl_context,
    GstDXGIDevice * dxgi_device)
{
  GstDXGIDeviceInterop *device;
  ID3D12Device *d3d12_device = (ID3D12Device *) dxgi_device->native_device;

  device = g_object_new (GST_TYPE_DXGI_DEVICE_INTEROP, NULL);
  device->wgl_funcs = wgl_functions_new(gl_context);
  device->dxgi_device = dxgi_device;

  // GST_ERROR("Native Device: %llu, Node Count: %llu", dxgi_device->native_device,
  //    ID3D12Device_GetNodeCount(d3d12_device));

  //g_critical("wgl open device");
  device->device_interop_handle = device->wgl_funcs->wglDXOpenDeviceNV(d3d12_device);
  //g_critical("wgl open device bam son");

  gst_object_ref_sink (dxgi_device);
  gst_object_ref_sink (device);

  return device;
}

void
gst_dxgi_device_interop_set_share_context (GstGLContext * context,
    GstDXGIDeviceInterop * device)
{
  gpointer d = g_object_get_data ((GObject*) context, GST_DXGI_DEVICE_INTEROP_CONTEXT);
  if (d != NULL) {
    GST_TRACE("D3D11 device already initialized");
    return;
  }

  g_object_set_data ((GObject *) context, GST_DXGI_DEVICE_INTEROP_CONTEXT, device);
}

GstDXGIDeviceInterop *
gst_dxgi_device_interop_from_share_context (GstGLContext * context) {
  GstDXGIDeviceInterop *share_context;
  share_context = (GstDXGIDeviceInterop *) g_object_get_data ((GObject*) context,
      GST_DXGI_DEVICE_INTEROP_CONTEXT);
  return share_context;
}

static void
_gst_dxgi_device_interop_new (GstGLContext * context, gpointer element) {
  if (gst_dxgi_device_interop_from_share_context(context) != NULL) {
    return;
  }

  GstDXGIDeviceInterop *device;
  GstDXGIDevice *dxgi_device;

  dxgi_device = g_object_new (GST_TYPE_DXGI_DEVICE_D3D12, NULL);
  device = gst_dxgi_device_interop_new_wrapped (context, dxgi_device);

  gst_dxgi_device_interop_set_share_context (context, device);
}

gboolean
gst_dxgi_device_interop_ensure_context (GstElement * self, GstGLContext ** context,
    GstGLContext ** other_context, GstGLDisplay ** display)
{
  GError *error = NULL;

  gst_dxgi_device_interop_init_once();

  if (*context) {
    gst_gl_context_thread_add (*context, (GstGLContextThreadFunc) _gst_dxgi_device_interop_new,
        self);
    return TRUE;
  }

  if (!*context) {
    gst_gl_ensure_element_data (GST_ELEMENT(self),
      display,
      other_context);
  }

  if (!*context) {
    GST_OBJECT_LOCK (*display);
    do {
      if (*context) {
        gst_object_unref (*context);
        *context = NULL;
      }

      *context = gst_gl_display_get_gl_context_for_thread (*display, NULL);
      if (!*context) {
        if (!gst_gl_display_create_context (*display, *other_context,
              context, &error)) {
          GST_OBJECT_UNLOCK (*display);
          goto context_error;
        }
      }
    } while (!gst_gl_display_add_context (*display, *context));
    GST_OBJECT_UNLOCK (*display);
  }

  gst_gl_context_thread_add (*context, (GstGLContextThreadFunc) _gst_dxgi_device_interop_new,
      self);

  return TRUE;

context_error:
  {
    if (error) {
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, ("%s", error->message),
          (NULL));
      g_clear_error (&error);
    } else {
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, (NULL), (NULL));
    }
    if (*context) {
      gst_object_unref (*context);
    }
    *context = NULL;
    return FALSE;
  }
}

static GstWGLFunctions *
wgl_functions_new (GstGLContext * gl_context) {
  GST_INFO("GL_VENDOR  : %s", glGetString(GL_VENDOR));
  GST_INFO("GL_VERSION : %s", glGetString(GL_VERSION));

  GstWGLFunctions *func = g_new(GstWGLFunctions, 1);

  func->wglDXOpenDeviceNV = (PFNWGLDXOPENDEVICENVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXOpenDeviceNV");
  func->wglDXCloseDeviceNV = (PFNWGLDXCLOSEDEVICENVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXCloseDeviceNV");
  func->wglDXRegisterObjectNV = (PFNWGLDXREGISTEROBJECTNVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXRegisterObjectNV");
  func->wglDXUnregisterObjectNV = (PFNWGLDXUNREGISTEROBJECTNVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXUnregisterObjectNV");
  func->wglDXLockObjectsNV = (PFNWGLDXLOCKOBJECTSNVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXLockObjectsNV");
  func->wglDXUnlockObjectsNV = (PFNWGLDXUNLOCKOBJECTSNVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXUnlockObjectsNV");
  func->wglDXSetResourceShareHandleNV = (PFNWGLDXSETRESOURCESHAREHANDLENVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXSetResourceShareHandleNV");

  return func;
}

