/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdxgidevice_base.h"

#include <windows.h>
#include <dxgi.h>
#include <d3d11_4.h>

static void gst_dxgi_device_set_multithread_protection (GstDXGIDevice * device,
    gboolean protection);

const static D3D_FEATURE_LEVEL d3d_feature_levels[] =
{
  D3D_FEATURE_LEVEL_11_1,
  D3D_FEATURE_LEVEL_11_0,
  D3D_FEATURE_LEVEL_10_1
};


const static GUID IID_ID3D112Multithread = {
  0x9B7E4E00, 0x342C, 0x4106, {0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0}
};

static void
gst_dxgi_device_class_init (GstDXGIDeviceClass * klass)
{
}

static void
gst_dxgi_device_init (GstDXGIDevice * device)
{
}

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

  gst_dxgi_device_set_multithread_protection (device, TRUE);

  return device;
}

static void gst_dxgi_device_set_multithread_protection (GstDXGIDevice * device,
    gboolean protection)
{
  ID3D11Device *d3d_device;
  ID3D11Multithread *mt;
  HRESULT hr;

  d3d_device = device->native_device;
  hr = d3d_device->lpVtbl->QueryInterface(d3d_device, &IID_ID3D112Multithread,
      (void**) &mt);

  if (hr == S_OK) {
    mt->lpVtbl->SetMultithreadProtected(mt, protection);
  }
}

GstDXGIDevice *
gst_dxgi_device_new ()
{
  GstGLDisplayEGL *ret;

  GST_DEBUG_CATEGORY_GET (gst_gl_display_debug, "gldisplay");

  ret = g_object_new (GST_TYPE_GL_DISPLAY_EGL, NULL);
  gst_object_ref_sink (ret);
  ret->display =
      gst_gl_display_egl_get_from_native (GST_GL_DISPLAY_TYPE_ANY, 0);

  if (!ret->display) {
    GST_INFO ("Failed to open EGL display connection");
  }

  return ret;
}

