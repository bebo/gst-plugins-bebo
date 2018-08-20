#ifndef __GST_DXGI_DEVICE_INCLUDED__
#define __GST_DXGI_DEVICE_INCLUDED__

#include <windows.h>
#include <GL/gl.h>
#include <d3d11.h>
#include <dxgi.h>
#include <gst/gst.h>
#include <gst/gl/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>

#define GST_GL_DXGI_D3D11_CONTEXT "gst_gl_dxgi_d3d11_context"

typedef struct _GstDXGID3D11Context
{
  ID3D11Device * d3d11_device;
  HANDLE device_interop_handle;
  PFNWGLDXOPENDEVICENVPROC wglDXOpenDeviceNV;
  PFNWGLDXCLOSEDEVICENVPROC wglDXCloseDeviceNV;
  PFNWGLDXREGISTEROBJECTNVPROC wglDXRegisterObjectNV;
  PFNWGLDXUNREGISTEROBJECTNVPROC wglDXUnregisterObjectNV;
  PFNWGLDXLOCKOBJECTSNVPROC wglDXLockObjectsNV;
  PFNWGLDXUNLOCKOBJECTSNVPROC wglDXUnlockObjectsNV;
  PFNWGLDXSETRESOURCESHAREHANDLENVPROC wglDXSetResourceShareHandleNV;
} GstDXGID3D11Context;

GstDXGID3D11Context * get_dxgi_share_context(GstGLContext * context);
gboolean gst_dxgi_device_ensure_gl_context(GstElement * element, GstGLContext ** context, GstGLContext ** other_context, GstGLDisplay ** display);

#endif /* __GST_NVENC_H_INCLUDED__ */
