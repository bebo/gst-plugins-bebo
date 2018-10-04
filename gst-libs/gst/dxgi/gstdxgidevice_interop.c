/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdxgidevice_interop.h"

/* prototypes */
static GstWGLFunctions* wgl_functions_new(GstGLContext * gl_context);

static void
gst_dxgi_device_interop_class_init (GstDXGIDeviceInteropClass * klass)
{
  GstGLContextClass *context_class = (GstGLContextClass *) klass;
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
gst_dxgi_device_interop_new_wrapped (GstGLContext * gl_context, GstDXGIDevice * dxgi_device)
{
  GstDXGIDeviceInterop *device;

  device = g_object_new (GST_TYPE_DXGI_DEVICE_INTEROP, NULL);
  device->wgl_funcs = wgl_functions_new(gl_context);
  device->dxgi_device = dxgi_device;
  device->device_interop_handle = device->wgl_funcs->wglDXOpenDeviceNV(dxgi_device->native_device);

  gst_object_ref (dxgi_device);
  gst_object_ref_sink (device);

  return device;
}

static GstDXGID3D11Context *
gst_dxgi_device_interop_set_share_context(GstGLContext * context, GstDXGIDeviceInterop * device)
{
  gpointer d = g_object_get_data((GObject*) context, GST_DXGI_DEVICE_INTEROP_CONTEXT);
  if (d != NULL) {
    GST_CAT_TRACE(GST_CAT_GL_DXGI, "D3D11 device already initialized");
    return;
  }

  g_object_set_data((GObject *) context, GST_DXGI_DEVICE_INTEROP_CONTEXT, device);
}

GstDXGID3D11Context *
gst_dxgi_device_interop_from_share_context(GstGLContext * context) {
  GstDXGIDeviceInterop *share_context;
  share_context = (GstDXGIDeviceInterop *) g_object_get_data((GObject*) context, GST_DXGI_DEVICE_INTEROP_CONTEXT);
  return share_context;
}

static GstWGLFunctions *
wgl_functions_new(GstGLContext * gl_context) {
  GST_CAT_INFO(GST_CAT_GL_DXGI, "GL_VENDOR  : %s", glGetString(GL_VENDOR));
  GST_CAT_INFO(GST_CAT_GL_DXGI, "GL_VERSION : %s", glGetString(GL_VERSION));
  g_assert(strcmp(glGetString(GL_VENDOR), "Intel") != 0);

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

