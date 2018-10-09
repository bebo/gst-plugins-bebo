/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifndef __GST_DXGI_DEVICE_BASE_H__
#define __GST_DXGI_DEVICE_BASE_H__

#include <gst/gst.h>

G_BEGIN_DECLS

G_GNUC_INTERNAL GType gst_dxgi_device_get_type (void);
#define GST_TYPE_DXGI_DEVICE            (gst_dxgi_device_get_type())

#define GST_DXGI_DEVICE(o)              (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_DXGI_DEVICE, GstDXGIDevice))
#define GST_DXGI_DEVICE_CLASS(k)        (G_TYPE_CHECK_CLASS((k), GST_TYPE_DXGI_DEVICE, GstDXGIDeviceClass))
#define GST_IS_DXGI_DEVICE(o)           (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_DXGI_DEVICE))
#define GST_IS_DXGI_DEVICE_CLASS(k)     (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_DXGI_DEVICE))
#define GST_DXGI_DEVICE_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_DXGI_DEVICE, GstDXGIDeviceClass))

typedef struct _GstDXGIDevice {
  /*< private >*/
  GstObject parent;

  /*< public >*/
  gpointer native_device;

  /*< private >*/
  gpointer _reserved[GST_PADDING];

} GstDXGIDevice;

typedef struct _GstDXGIDeviceClass {
  /*< private >*/
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _reserved[GST_PADDING];

} GstDXGIDeviceClass;


GstDXGIDevice *
gst_dxgi_device_new ();

G_END_DECLS


#endif /* __GST_DXGI_DEVICE_BASE_H__ */
