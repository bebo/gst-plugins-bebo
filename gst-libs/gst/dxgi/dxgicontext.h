/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifndef __GST_DXGI_CONTEXT_H__
#define __GST_DXGI_CONTEXT_H__

#include <gst/gst.h>
#include "dxgidisplay.h"

G_BEGIN_DECLS

G_GNUC_INTERNAL GType gst_dxgi_context_get_type (void);
#define GST_TYPE_DXGI_CONTEXT            (gst_dxgi_context_get_type())
#define GST_DXGI_CONTEXT(o)              (G_TYPE_CHECK_INSTANCE_CAST((o), \
      GST_TYPE_DXGI_CONTEXT, GstDXGIContext))
#define GST_DXGI_CONTEXT_CLASS(k)        (G_TYPE_CHECK_CLASS((k), \
      GST_TYPE_DXGI_CONTEXT, GstDXGIContextClass))
#define GST_IS_DXGI_CONTEXT(o)           (G_TYPE_CHECK_INSTANCE_TYPE((o), \
      GST_TYPE_DXGI_CONTEXT))
#define GST_IS_DXGI_CONTEXT_CLASS(k)     (G_TYPE_CHECK_CLASS_TYPE((k), \
      GST_TYPE_DXGI_CONTEXT))
#define GST_DXGI_CONTEXT_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS((o), \
      GST_TYPE_DXGI_CONTEXT, GstDXGIContextClass))

typedef struct _GstDXGIContext {
  /*< private >*/
  GstObject parent;

  /*< public >*/
  GstDXGIDisplay *display;

  /*< private >*/
  gpointer _reserved[GST_PADDING];
} GstDXGIContext;

typedef struct _GstDXGIContextClass {
  /*< private >*/
  GstObjectClass parent_class;

  guintptr      (*get_current_context) (void);
  guintptr      (*get_context)        (GstDXGIContext *context);
  gpointer      (*get_proc_address)   (const gchar *name);
  gboolean      (*activate)           (GstDXGIContext *context, gboolean activate);
  gboolean      (*choose_format)      (GstDXGIContext *context, GError **error);
  gboolean      (*create_context)     (GstDXGIContext *context,
                                       GstDXGIContext *other_context, GError ** error);
  void          (*destroy_context)    (GstDXGIContext *context);
  void          (*swap_buffers)       (GstDXGIContext *context);
  gboolean      (*check_feature)      (GstDXGIContext *context, const gchar *feature);
  void          (*get_platform_version) (GstDXGIContext *context, gint *major, gint *minor);

  /*< private >*/
  gpointer _reserved[GST_PADDING];
} GstDXGIContextClass;

/* methods */
GstDXGIContext * gst_dxgi_context_new (GstDXGIDisplay *display);
GstDXGIContext * gst_dxgi_context_get_current (void);
GstDXGIDisplay * gst_dxgi_context_get_display (GstDXGIContext *context);
gpointer         gst_dxgi_context_get_proc_address (GstDXGIContext *context,
    const gchar *name);



G_END_DECLS


#endif /* __GST_DXGI_CONTEXT_H__ */
