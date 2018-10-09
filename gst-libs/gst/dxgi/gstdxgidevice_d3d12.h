/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifndef __GST_DXGI_DEVICE_D3D12_H__
#define __GST_DXGI_DEVICE_D3D12_H__

#include <gst/gst.h>

#include "gstdxgidevice_base.h"

G_BEGIN_DECLS

G_GNUC_INTERNAL GType gst_dxgi_device_d3d12_get_type (void);
#define GST_TYPE_DXGI_DEVICE_D3D12            (gst_dxgi_device_d3d12_get_type())

#define GST_DXGI_DEVICE_D3D12(o)              (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_DXGI_DEVICE_D3D12, GstDXGIDeviceD3D12))
#define GST_DXGI_DEVICE_D3D12_CLASS(k)        (G_TYPE_CHECK_CLASS((k), GST_TYPE_DXGI_DEVICE_D3D12, GstDXGIDeviceD3D12Class))
#define GST_IS_DXGI_DEVICE_D3D12(o)           (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_DXGI_DEVICE_D3D12))
#define GST_IS_DXGI_DEVICE_D3D12_CLASS(k)     (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_DXGI_DEVICE_D3D12))
#define GST_DXGI_DEVICE_D3D12_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_DXGI_DEVICE_D3D12, GstDXGIDeviceD3D12Class))

typedef struct _GstDXGIDeviceD3D12 {
  /*< private >*/
  GstDXGIDevice parent;

  /*< public >*/

  /*< private >*/
  gpointer _reserved[GST_PADDING];

} GstDXGIDeviceD3D12;

typedef struct _GstDXGIDeviceD3D12Class {
  /*< private >*/
  GstDXGIDeviceClass parent_class;

  /*< private >*/
  gpointer _reserved[GST_PADDING];

} GstDXGIDeviceD3D12Class;


G_END_DECLS


#endif /* __GST_DXGI_DEVICE_D3D12_H__ */
