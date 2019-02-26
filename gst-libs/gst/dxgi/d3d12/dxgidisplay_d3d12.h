/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifndef __GST_DXGI_DISPLAY_D3D12_H__
#define __GST_DXGI_DISPLAY_D3D12_H__

#include <gst/dxgi/dxgidisplay.h>

G_BEGIN_DECLS

GType gst_dxgi_display_d3d12_get_type (void);

#define GST_TYPE_DXGI_DISPLAY_D3D12             (gst_dxgi_display_d3d12_get_type())
#define GST_DXGI_DISPLAY_D3D12(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DXGI_DISPLAY_D3D12,GstDXGIDisplayD3D12))
#define GST_DXGI_DISPLAY_D3D12_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DXGI_DISPLAY_D3D12,GstDXGIDisplayD3D12Class))
#define GST_IS_DXGI_DISPLAY_D3D12(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DXGI_DISPLAY_D3D12))
#define GST_IS_DXGI_DISPLAY_D3D12_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DXGI_DISPLAY_D3D12))
#define GST_DXGI_DISPLAY_D3D12_CAST(obj)        ((GstDXGIDisplayD3D12*)(obj))

typedef struct _GstDXGIDisplayD3D12      GstDXGIDisplayD3D12;
typedef struct _GstDXGIDisplayD3D12Class GstDXGIDisplayD3D12Class;


struct _GstDXGIDisplayD3D12
{
  GstDXGIDisplay          parent;

  gpointer _padding[GST_PADDING];
};

struct _GstDXGIDisplayD3D12Class
{
  GstDXGIDisplayClass object_class;

  gpointer _padding[GST_PADDING];
};

GstDXGIDisplayD3D12 *gst_dxgi_display_d3d12_new (void);


G_END_DECLS


#endif /* __GST_DXGI_DISPLAY_D3D12_H__ */
