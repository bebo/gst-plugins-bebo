/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifndef __GST_DXGI_CONTEXT_H__
#define __GST_DXGI_CONTEXT_H__

#include <gst/gst.h>

G_BEGIN_DECLS

G_GNUC_INTERNAL GType gst_dxgi_context_get_type (void);
#define GST_TYPE_DXGI_CONTEXT            (gst_dxgi_context_get_type())

#define GST_DXGI_CONTEXT(o)              (G_TYPE_CHECK_INSTANCE_CAST((o), GST_TYPE_DXGI_CONTEXT, GstDXGIContext))
#define GST_DXGI_CONTEXT_CLASS(k)        (G_TYPE_CHECK_CLASS((k), GST_TYPE_DXGI_CONTEXT, GstDXGIContextClass))
#define GST_IS_DXGI_CONTEXT(o)           (G_TYPE_CHECK_INSTANCE_TYPE((o), GST_TYPE_DXGI_CONTEXT))
#define GST_IS_DXGI_CONTEXT_CLASS(k)     (G_TYPE_CHECK_CLASS_TYPE((k), GST_TYPE_DXGI_CONTEXT))
#define GST_DXGI_CONTEXT_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS((o), GST_TYPE_DXGI_CONTEXT, GstDXGIContextClass))

typedef struct _GstDXGIContext {
  /*< private >*/
  GstObject parent;

  /*< public >*/

  /*< private >*/
  gpointer _reserved[GST_PADDING];

} GstDXGIContext;

typedef struct _GstDXGIContextClass {
  /*< private >*/
  GstObjectClass parent_class;

  /*< private >*/
  gpointer _reserved[GST_PADDING];

} GstDXGIContextClass;


G_END_DECLS


#endif /* __GST_DXGI_CONTEXT_H__ */
