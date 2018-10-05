/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifndef __GST_DXGI_CONTEXT_H__
#define __GST_DXGI_CONTEXT_H__

#include <gst/gst.h>

G_BEGIN_DECLS

G_GNUC_INTERNAL GType gst_dxgi_device_get_type (void);
#define GST_TYPE_DXGI_DEVICE_D3D11            (gst_dxgi_device_get_type())

#define GST_DXGI_DEVICE_D3D11(o)              (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_DXGI_DEVICE_D3D11, GstDXGIDevice))
#define GST_DXGI_DEVICE_D3D11_CLASS(k)        (G_TYPE_CHECK_CLASS((k), GST_TYPE_DXGI_DEVICE_D3D11, GstDXGIDeviceClass))
#define GST_IS_DXGI_DEVICE_D3D11(o)           (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_DXGI_DEVICE_D3D11))
#define GST_IS_DXGI_DEVICE_D3D11_CLASS(k)     (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_DXGI_DEVICE_D3D11))
#define GST_DXGI_DEVICE_D3D11_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_DXGI_DEVICE_D3D11, GstDXGIDeviceClass))

typedef struct _GstDXGIDeviceD3D11 {
  /*< private >*/
  GstDXGIDevice parent;

  /*< public >*/
  gpointer native_device;

  /*< private >*/
  gpointer _reserved[GST_PADDING];

} GstDXGIDeviceD3D11;

typedef struct _GstDXGIDeviceD3D11Class {
  /*< private >*/
  GstDXGIDeviceClass parent_class;

  /*< private >*/
  gpointer _reserved[GST_PADDING];

} GstDXGIDeviceD3D11Class;


GstDXGIDeviceD3D11 *
gst_dxgi_device_new ();

G_END_DECLS


#endif /* __GST_DXGI_CONTEXT_H__ */
