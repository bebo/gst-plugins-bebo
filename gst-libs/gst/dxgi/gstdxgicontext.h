/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifndef __GST_DXGI_CONTEXT_H__
#define __GST_DXGI_CONTEXT_H__

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <gst/gst.h>
#include <gst/gl/gl.h>

#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>

G_BEGIN_DECLS

struct _GstDXGIDevice {
  /*< private >*/
  GstObject parent;
};

struct _GstDXGIDeviceClass {
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _reserved[GST_PADDING];
};

struct _GstDXGIContext {
  /*< private >*/
  GstObject parent;

  /*< public >*/
  GstDXGIDevice *device;

  /*< private >*/
  gpointer _reserved[GST_PADDING];
};

struct _GstDXGIContextClass {
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _reserved[GST_PADDING];
};

struct _GstDXGIInteropDevice {
  GstDXGIDevice parent;

  HANDLE device_interop_handle;
  PFNWGLDXOPENDEVICENVPROC wglDXOpenDeviceNV;
  PFNWGLDXCLOSEDEVICENVPROC wglDXCloseDeviceNV;
  PFNWGLDXREGISTEROBJECTNVPROC wglDXRegisterObjectNV;
  PFNWGLDXUNREGISTEROBJECTNVPROC wglDXUnregisterObjectNV;
  PFNWGLDXLOCKOBJECTSNVPROC wglDXLockObjectsNV;
  PFNWGLDXUNLOCKOBJECTSNVPROC wglDXUnlockObjectsNV;
  PFNWGLDXSETRESOURCESHAREHANDLENVPROC wglDXSetResourceShareHandleNV;
};

struct _GstDXGIInteropDeviceClass {
  GstDXGIInteropDevice parent;
};

G_END_DECLS

#endif /* __GST_DXGI_CONTEXT_H__ */
