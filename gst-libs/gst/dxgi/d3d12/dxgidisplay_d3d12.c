/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dxgidisplay_d3d12.h"

#define COBJMACROS
#define CINTERFACES
#include <dxgi.h>
#include <d3d12.h>


GST_DEBUG_CATEGORY_STATIC (gst_dxgi_display_debug);
#define GST_CAT_DEFAULT gst_dxgi_display_debug

G_DEFINE_TYPE (GstDXGIDisplayD3D12, gst_dxgi_display_d3d12, GST_TYPE_DXGI_DISPLAY);


static void gst_dxgi_display_d3d12_finalize (GObject * object);
static guintptr gst_dxgi_display_d3d12_get_handle (GstDXGIDisplay * display);
static IDXGIAdapter1 * gst_dxgi_display_d3d12_get_adapter ();
static ID3D12Device * gst_dxgi_display_d3d12_create_device (IDXGIAdapter1 *adapter);

static void
gst_dxgi_display_d3d12_class_init (GstDXGIDisplayD3D12Class * klass)
{
  G_OBJECT_CLASS (klass)->finalize = gst_dxgi_display_d3d12_finalize;
}

static void
gst_dxgi_display_d3d12_init (GstDXGIDisplayD3D12 * display_d3d12)
{
  GstDXGIDisplay *display = (GstDXGIDisplay *) display_d3d12;

  display->type = GST_DXGI_DISPLAY_TYPE_DX12;

#if 0
  gst_gl_memory_d3d12_init_once ();
#endif

}

static void
gst_dxgi_display_d3d12_finalize (GObject * object)
{
  GstDXGIDisplayD3D12 *display = GST_DXGI_DISPLAY_D3D12 (object);

  G_OBJECT_CLASS (gst_dxgi_display_d3d12_parent_class)->finalize (object);
}

GstDXGIDisplayD3D12 *
gst_dxgi_display_d3d12_new (void)
{
  GstDXGIDisplay *base;
  GstDXGIDisplayD3D12 *ret;

  GST_DEBUG_CATEGORY_GET (gst_dxgi_display_debug, "dxgidisplay");

  ret = g_object_new (GST_TYPE_DXGI_DISPLAY_D3D12, NULL);
  gst_object_ref_sink (ret);

  base = GST_DXGI_DISPLAY (ret);

  base->adapter = gst_dxgi_display_d3d12_get_adapter ();
  if (!base->adapter) {
    GST_ERROR ("Failed to get D3D12 adapter");
  }

  base->device = gst_dxgi_display_d3d12_create_device ((IDXGIAdapter1 *) base->adapter);
  if (!base->device) {
    GST_ERROR ("Failed to create D3D12 device");
  }

  return ret;
}

IDXGIAdapter1 *
gst_dxgi_display_d3d12_get_adapter ()
{
  GPtrArray *adapters;
  IDXGIAdapter1 *adapter;
  IDXGIAdapter1 *preferred_adapter = NULL;
  IDXGIFactory1 *factory;
  HRESULT hr;
  guint32 adapters_length = 0;

  hr = CreateDXGIFactory (&IID_IDXGIFactory1, &factory);
  if (FAILED(hr)) {
    return NULL;
  }

  adapters = g_ptr_array_new ();

  for (guint32 i = 0; IDXGIFactory1_EnumAdapters1 (factory, i, &adapter); ++i) {
    DXGI_ADAPTER_DESC desc;
    gboolean isUMA = FALSE;

    hr = IDXGIAdapter1_GetDesc (adapter, &desc);
    if (FAILED (hr)) {
      continue;
    }

    // ignore Microsoft renderer
    if (desc.VendorId == 0x1414 && desc.DeviceId == 0x8c) {
      continue;
    }

    // https://github.com/GameTechDev/gpudetect/blob/bbfc86b46cbfffe3c7bfc443064c51fd6c1b910b/DeviceId.cpp#L51
    // To check is it's integrated or not.
    if (desc.VendorId == 0x8086 && desc.DedicatedVideoMemory <= 512 * 1024 * 1024) {
      isUMA = TRUE;
    }

    if (!isUMA) {
      preferred_adapter = adapter;
    }

    adapters_length += 1;
    g_ptr_array_add(adapters, adapter);
  }

  if (preferred_adapter == NULL) {
    if (adapters_length == 0) {
      return NULL;
    }
    preferred_adapter = (IDXGIAdapter1 *) g_ptr_array_index (adapters, 0);
  }

  for (guint32 i = 0; i < adapters_length; i++) {
    DXGI_ADAPTER_DESC desc;

    adapter = (IDXGIAdapter1 *) g_ptr_array_index (adapters, i);

    hr = IDXGIAdapter1_GetDesc (adapter, &desc);
    if (FAILED (hr)) {
      continue;
    }

    // TODO: log desc.Description, desc.VendorID, desc.desc.DeviceID

    if (adapter != preferred_adapter) {
      IDXGIAdapter1_Release (adapter);
    }
  }

  IDXGIFactory_Release (factory);

  g_ptr_array_free (adapters, FALSE);

  return preferred_adapter;
}

ID3D12Device *
gst_dxgi_display_d3d12_create_device (IDXGIAdapter1 *adapter)
{
  D3D_FEATURE_LEVEL min_feature_level;
  ID3D12Device *device;
  HRESULT hr;

  min_feature_level = D3D_FEATURE_LEVEL_11_0;
  hr = D3D12CreateDevice ((IUnknown *) adapter, min_feature_level,
      &IID_ID3D12Device, &device);

  if (FAILED(hr)) {
    return NULL;
  }

  return device;
}
