/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifndef __GST_DXGI_DEVICE_INTEROP_H__
#define __GST_DXGI_DEVICE_INTEROP_H__

#include <gst/dxgi/gstdxgidevice_base.h>

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <gst/gst.h>
#include <gst/gl/gl.h>

#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>

G_BEGIN_DECLS

G_GNUC_INTERNAL GType   gst_dxgi_device_interop_get_type (void);
#define GST_TYPE_DXGI_DEVICE_INTEROP    (gst_dxgi_device_interop_get_type())

#define GST_DXGI_DEVICE_INTEROP(o)      (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_DXGI_DEVICE_INTEROP, GstDXGIDeviceInterop))
#define GST_DXGI_DEVICE_INTEROP_CLASS(k)        (G_TYPE_CHECK_CLASS((k), GST_TYPE_DXGI_DEVICE_INTEROP, GstDXGIDeviceInteropClass))
#define GST_IS_DXGI_DEVICE_INTEROP(o)           (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_DXGI_DEVICE_INTEROP))
#define GST_IS_DXGI_DEVICE_INTEROP_CLASS(k)     (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_DXGI_DEVICE_INTEROP))
#define GST_DXGI_DEVICE_INTEROP_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_DXGI_DEVICE_INTEROP, GstDXGIDeviceInteropClass))

#define GST_DXGI_DEVICE_INTEROP_CONTEXT "gst_dxgi_device_interop_context"

typedef struct _GstWGLFunctions {
  PFNWGLDXOPENDEVICENVPROC wglDXOpenDeviceNV;
  PFNWGLDXCLOSEDEVICENVPROC wglDXCloseDeviceNV;
  PFNWGLDXREGISTEROBJECTNVPROC wglDXRegisterObjectNV;
  PFNWGLDXUNREGISTEROBJECTNVPROC wglDXUnregisterObjectNV;
  PFNWGLDXLOCKOBJECTSNVPROC wglDXLockObjectsNV;
  PFNWGLDXUNLOCKOBJECTSNVPROC wglDXUnlockObjectsNV;
  PFNWGLDXSETRESOURCESHAREHANDLENVPROC wglDXSetResourceShareHandleNV;
} GstWGLFunctions;

typedef struct _GstDXGIDeviceInterop {
  /*< private >*/
  GstObject parent;

  GstDXGIDevice *dxgi_device;
  HANDLE device_interop_handle;
  GstWGLFunctions *wgl_funcs;

  /*< private >*/
  gpointer _reserved[GST_PADDING];

} GstDXGIDeviceInterop;

typedef struct _GstDXGIDeviceInteropClass {
  /*< private >*/
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _reserved[GST_PADDING];

} GstDXGIDeviceInteropClass;


GstDXGIDeviceInterop *
gst_dxgi_device_interop_new_wrapped (GstGLContext * gl_context, GstDXGIDevice * dxgi_device);

GstDXGIDeviceInterop *
gst_dxgi_device_interop_from_share_context (GstGLContext * gl_context);

void
gst_dxgi_device_interop_set_share_context (GstGLContext * context,
    GstDXGIDeviceInterop * device);

G_END_DECLS

#endif /* __GST_DXGI_DEVICE_INTEROP_H__ */
