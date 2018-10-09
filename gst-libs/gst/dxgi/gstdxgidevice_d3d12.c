/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstdxgidevice_d3d12.h"

#include <windows.h>
#include <dxgi.h>
#include <d3d11_4.h>
#include <d3d12.h>

G_DEFINE_TYPE (GstDXGIDeviceD3D12, gst_dxgi_device_d3d12, GST_TYPE_DXGI_DEVICE);

static void gst_dxgi_device_d3d12_finalize (GObject * object);

static IDXGIAdapter1 * gst_dxgi_device_get_adapter (GstDXGIDevice * device);
static void gst_dxgi_device_set_multithread_protection (GstDXGIDevice * device,
    gboolean protection);

#if 0
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
#endif

static void
gst_dxgi_device_d3d12_class_init (GstDXGIDeviceD3D12Class * klass)
{
  G_OBJECT_CLASS (klass)->finalize = gst_dxgi_device_d3d12_finalize;
}

static void
gst_dxgi_device_d3d12_init (GstDXGIDeviceD3D12 * device)
{
}

static void
gst_dxgi_device_d3d12_finalize (GObject * object)
{
  GstDXGIDevice *base = GST_DXGI_DEVICE (object);
  GstDXGIDeviceD3D12 *self = GST_DXGI_DEVICE_D3D12 (object);
  ID3D12Device *d3d_device;

  if (base->native_device) {
    d3d_device = (ID3D12Device *) base->native_device;
    // d3d_device->Release();
    // FIXME: how the fuck do you free these monster?
  }

  G_OBJECT_CLASS (gst_dxgi_device_d3d12_parent_class)->finalize (object);
}


static IDXGIAdapter1 *
gst_dxgi_device_get_adapter (GstDXGIDevice * device) {
  // TODO
  return NULL;
}

static void
gst_dxgi_device_create_device (GstDXGIDevice * device) {
  GstDXGIDevice *base = GST_DXGI_DEVICE (device);

  HRESULT hr;
  D3D_FEATURE_LEVEL min_feature_level;
  IDXGIAdapter1 *adapter;

  min_feature_level = D3D_FEATURE_LEVEL_12_0;
  adapter = gst_dxgi_device_get_adapter (device);

  hr = D3D12CreateDevice(adapter, min_feature_level, &IID_ID3D12Device,
      &base->native_device);

  gst_dxgi_device_set_multithread_protection (device, TRUE);
}

static void gst_dxgi_device_set_multithread_protection (GstDXGIDevice * device,
    gboolean protection)
{
  GstDXGIDevice *base = GST_DXGI_DEVICE (device);

  ID3D12Device *d3d_device;
  ID3D11Multithread *mt;
  HRESULT hr;

  d3d_device = (ID3D12Device *) base->native_device;
  hr = d3d_device->lpVtbl->QueryInterface(d3d_device, &IID_ID3D11Multithread,
      (void**) &mt);

  if (hr == S_OK) {
    mt->lpVtbl->SetMultithreadProtected(mt, protection);
  }
}

