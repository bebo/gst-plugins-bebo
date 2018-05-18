#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdxgidevice.h"
#include <D3d11_4.h>


static void init_wgl_functions(GstGLContext* gl_context, GstDXGID3D11Context *share_context) {
  GST_INFO("GL_VENDOR  : %s", glGetString(GL_VENDOR));
  GST_INFO("GL_VERSION : %s", glGetString(GL_VERSION));

  share_context->wglDXOpenDeviceNV = (PFNWGLDXOPENDEVICENVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXOpenDeviceNV");
  share_context->wglDXCloseDeviceNV = (PFNWGLDXCLOSEDEVICENVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXCloseDeviceNV");
  share_context->wglDXRegisterObjectNV = (PFNWGLDXREGISTEROBJECTNVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXRegisterObjectNV");
  share_context->wglDXUnregisterObjectNV = (PFNWGLDXUNREGISTEROBJECTNVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXUnregisterObjectNV");
  share_context->wglDXLockObjectsNV = (PFNWGLDXLOCKOBJECTSNVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXLockObjectsNV");
  share_context->wglDXUnlockObjectsNV = (PFNWGLDXUNLOCKOBJECTSNVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXUnlockObjectsNV");
  share_context->wglDXSetResourceShareHandleNV = (PFNWGLDXSETRESOURCESHAREHANDLENVPROC)
    gst_gl_context_get_proc_address(gl_context, "wglDXSetResourceShareHandleNV");
}

const static D3D_FEATURE_LEVEL d3d_feature_levels[] =
{
  D3D_FEATURE_LEVEL_11_1,
  D3D_FEATURE_LEVEL_11_0,
  D3D_FEATURE_LEVEL_10_1
};

static ID3D11Device*
_create_device_d3d11() {
  ID3D11Device *device;

  D3D_FEATURE_LEVEL level_used = D3D_FEATURE_LEVEL_11_1;

  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#ifdef _DEBUG
  flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  HRESULT hr = D3D11CreateDevice(
      NULL,
      D3D_DRIVER_TYPE_HARDWARE,
      NULL,
      flags,
      d3d_feature_levels,
      sizeof(d3d_feature_levels) / sizeof(D3D_FEATURE_LEVEL),
      D3D11_SDK_VERSION,
      &device,
      &level_used,
      NULL);

  GST_INFO("CreateDevice HR: 0x%08x, level_used: 0x%08x (%d)", hr,
      (unsigned int) level_used, (unsigned int) level_used);
  
  GUID myIID_ID3D112Multithread = {
    0x9B7E4E00, 0x342C, 0x4106, {0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0} };

  ID3D11Multithread *mt;
  hr = (device)->lpVtbl->QueryInterface(device, &myIID_ID3D112Multithread,
      (void**)&mt);
  g_assert(hr == S_OK);
  mt->lpVtbl->SetMultithreadProtected(mt, TRUE);

  return device;
}

static void
_init_d3d11_context(GstGLContext* gl_context, gpointer * element) {

  void * d = g_object_get_data((GObject*)gl_context, GST_GL_DXGI_D3D11_CONTEXT);
  if (d != NULL) {
    GST_INFO("D3D11 device already initialized");
    return;
  }

  GstDXGID3D11Context *share_context = g_new(GstDXGID3D11Context, 1);

  init_wgl_functions(gl_context, share_context);

  share_context->d3d11_device = _create_device_d3d11();
  g_assert( share_context->d3d11_device != NULL);
  GST_INFO("created D3D11 device for %" GST_PTR_FORMAT, gl_context);

  share_context->device_interop_handle = share_context->wglDXOpenDeviceNV(share_context->d3d11_device);
  g_assert( share_context->device_interop_handle != NULL);

  g_object_set_data((GObject *) gl_context, GST_GL_DXGI_D3D11_CONTEXT, share_context);


  // TODO: need to free these memory and close interop
}

gboolean
gst_dxgi_device_ensure_gl_context(GstElement * self, GstGLContext** context, GstGLContext** other_context, GstGLDisplay ** display)
{
  GError *error = NULL;

  if (*context) {
    gst_gl_context_thread_add(*context, (GstGLContextThreadFunc) _init_d3d11_context, self);
    return TRUE;
  }

  if (!*context) {
    gst_gl_ensure_element_data(GST_ELEMENT(self),
      display,
      other_context);
  }
  GST_INFO_OBJECT(self, "other_context:%" GST_PTR_FORMAT, *other_context);

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
  gst_gl_context_thread_add(*context, (GstGLContextThreadFunc) _init_d3d11_context, self);
  GST_INFO_OBJECT(self, "context:%" GST_PTR_FORMAT, *context);


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
    if (*context)
      gst_object_unref (*context);
    *context = NULL;
    return FALSE;
  }
}

GstDXGID3D11Context * get_dxgi_share_context(GstGLContext * context) {
  GstDXGID3D11Context *share_context;
  share_context = (GstDXGID3D11Context*) g_object_get_data((GObject*) context, GST_GL_DXGI_D3D11_CONTEXT);
  return share_context;
}

