#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdxgidevice.h"
#include <D3d11_4.h>

GST_DEBUG_CATEGORY_STATIC (GST_CAT_GL_DXGI);
#define GST_CAT_DEFAULT GST_CAT_GL_DXGI

void
gst_gl_dxgi_device_init_once(void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter(&_init)) {
    GST_DEBUG_CATEGORY_INIT(GST_CAT_GL_DXGI, "gl2dxgidevice", 0, "OpenGL DXGI Device");
    g_once_init_leave(&_init, 1);
  }
}

static void init_wgl_functions(GstGLContext* gl_context, GstDXGID3D11Context *share_context) {
  GST_CAT_INFO(GST_CAT_GL_DXGI, "GL_VENDOR  : %s",
      glGetString(GL_VENDOR));
  GST_CAT_INFO(GST_CAT_GL_DXGI, "GL_VERSION : %s",
      glGetString(GL_VERSION));
  GST_CAT_INFO(GST_CAT_GL_DXGI, "GL_RENDERER : %s",
      glGetString(GL_RENDERER));
  GST_CAT_INFO(GST_CAT_GL_DXGI, "GL_SHADING_LANGUAGE_VERSION: %s",
      glGetString(GL_SHADING_LANGUAGE_VERSION));

  // g_assert(strcmp(glGetString(GL_VENDOR), "Intel") != 0);

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
  GST_CAT_INFO(GST_CAT_GL_DXGI, "CreateDevice HR: 0x%08x, level_used: 0x%08x (%d)", hr,
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

  gst_gl_dxgi_device_init_once();
  void * d = g_object_get_data((GObject*)gl_context, GST_GL_DXGI_D3D11_CONTEXT);
  if (d != NULL) {
    GST_CAT_TRACE(GST_CAT_GL_DXGI, "D3D11 device already initialized");
    return;
  }

  GstDXGID3D11Context *share_context = g_new(GstDXGID3D11Context, 1);

  init_wgl_functions(gl_context, share_context);

  share_context->d3d11_device = _create_device_d3d11();
  g_assert( share_context->d3d11_device != NULL);
  GST_CAT_INFO(GST_CAT_GL_DXGI, "created D3D11 device for %" GST_PTR_FORMAT, gl_context);

  share_context->device_interop_handle = share_context->wglDXOpenDeviceNV(share_context->d3d11_device);
  g_assert( share_context->device_interop_handle != NULL);

  g_object_set_data((GObject *) gl_context, GST_GL_DXGI_D3D11_CONTEXT, share_context);

  // TODO: need to free these memory and close interop
}

gboolean
gst_dxgi_device_ensure_gl_context(GstElement * self, GstGLContext** context, GstGLContext** other_context, GstGLDisplay ** display)
{
  GError *error = NULL;
  gst_gl_dxgi_device_init_once();

  if (*context) {
    gst_gl_context_thread_add(*context, (GstGLContextThreadFunc) _init_d3d11_context, self);
    return TRUE;
  }

  if (!*context) {
    gst_gl_ensure_element_data(GST_ELEMENT(self),
      display,
      other_context);
  }
  GST_CAT_DEBUG(GST_CAT_GL_DXGI, "other_context:%" GST_PTR_FORMAT, *other_context);

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
  GST_CAT_DEBUG(GST_CAT_GL_DXGI, "context:%" GST_PTR_FORMAT, *context);

  return TRUE;

context_error:
  {
    if (error) {
      GST_CAT_ERROR(GST_CAT_GL_DXGI, "RESOURCE NOT FOUND: %s", error->message);
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND, ("%s", error->message),
          (NULL));
      g_clear_error (&error);
    } else {
      GST_CAT_ERROR(GST_CAT_GL_DXGI, "RESOURCE NOT FOUND");
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
