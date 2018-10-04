#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdxgidevice_base.h"
#include <D3d11_4.h>

GST_DEBUG_CATEGORY_STATIC (GST_CAT_GL_DXGI);
#define GST_CAT_DEFAULT GST_CAT_GL_DXGI

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

GstDXGID3D11Context * get_dxgi_share_context(GstGLContext * context) {
  GstDXGID3D11Context *share_context;
  share_context = (GstDXGID3D11Context*) g_object_get_data((GObject*) context, GST_GL_DXGI_D3D11_CONTEXT);
  return share_context;
}
