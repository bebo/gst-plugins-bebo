/*
 * Copyright (c) 2019 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifndef __GST_DXGI_DISPLAY_H__
#define __GST_DXGI_DISPLAY_H__

#include <gst/gst.h>

G_BEGIN_DECLS

GType gst_dxgi_display_get_type (void);

#define GST_TYPE_DXGI_DISPLAY           (gst_dxgi_display_get_type())
#define GST_DXGI_DISPLAY(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DXGI_DISPLAY,GstDXGIDisplay))
#define GST_DXGI_DISPLAY_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_DXGI_DISPLAY,GstDXGIDisplayClass))
#define GST_IS_DXGI_DISPLAY(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DXGI_DISPLAY))
#define GST_IS_DXGI_DISPLAY_CLASS(klass)(G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_DXGI_DISPLAY))
#define GST_DXGI_DISPLAY_CAST(obj)      ((GstDXGIDisplay*)(obj))
#define GST_DXGI_DISPLAY_GET_CLASS(o)   (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_DXGI_DISPLAY, GstDXGIDisplayClass))

typedef enum
{
  GST_DXGI_DISPLAY_TYPE_NONE = 0,
  GST_DXGI_DISPLAY_TYPE_DX9 = (1 << 0),
  GST_DXGI_DISPLAY_TYPE_DX11 = (1 << 1),
  GST_DXGI_DISPLAY_TYPE_DX12 = (1 << 2),
  GST_DXGI_DISPLAY_TYPE_ANY = G_MAXUINT32
} GstDXGIDisplayType;

typedef struct _GstDXGIDisplay
{
  /* <private> */
  GstObject             object;

  gpointer adapter;
  gpointer device;

  GstDXGIDisplayType    type;
} GstDXGIDisplay;

typedef struct _GstDXGIDisplayClass
{
  GstObjectClass object_class;

  /* <private> */
  gpointer _padding[GST_PADDING];
} GstDXGIDisplayClass;

/* methods */
GstDXGIDisplay *gst_dxgi_display_new (void);


G_END_DECLS

#endif /* __GST_DXGI_DISPLAY_H__ */
