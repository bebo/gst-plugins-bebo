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
#include <d3d12.h>

static void gst_dxgi_device_set_multithread_protection (GstDXGIDevice * device,
    gboolean protection);

const static D3D_FEATURE_LEVEL d3d_feature_levels[] =
{
  D3D_FEATURE_LEVEL_10_0,
  D3D_FEATURE_LEVEL_10_1,
  D3D_FEATURE_LEVEL_11_0,
  D3D_FEATURE_LEVEL_11_1,
  D3D_FEATURE_LEVEL_12_0,
  D3D_FEATURE_LEVEL_12_1
};

const static GUID IID_ID3D11Multithread = {
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

static IDXGIAdapter1 *
gst_dxgi_device_get_adapter() {
  // TODO
}

static void
gst_dxgi_device_create_device (GstDXGIDevice * device) {
  D3D_FEATURE_LEVEL min_feature_level = D3D_FEATURE_LEVEL_12_0;
  IDXGIAdapter1 adapter = NULL;
  HRESULT hr;

  hr = D3D12CreateDevice(adapter, min_feature_level, IID_ID3D12Device,
      &device->native_device);

  gst_dxgi_device_set_multithread_protection (device, TRUE);

  return device;
}

static void gst_dxgi_device_set_multithread_protection (GstDXGIDevice * device,
    gboolean protection)
{
  ID3D12Device *d3d_device;
  ID3D11Multithread *mt;
  HRESULT hr;

  d3d_device = (ID3D12Device *) device->native_device;
  hr = d3d_device->lpVtbl->QueryInterface(d3d_device, &IID_ID3D11Multithread,
      (void**) &mt);

  if (hr == S_OK) {
    mt->lpVtbl->SetMultithreadProtected(mt, protection);
  }
}

