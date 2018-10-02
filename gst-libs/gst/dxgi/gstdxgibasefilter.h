/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifndef _GST_DXGI_BASE_FILTER_H_
#define _GST_DXGI_BASE_FILTER_H_

#include <gst/base/gstbasetransform.h>

G_BEGIN_DECLS

GType gst_dxgi_base_filter_get_type(void);
#define GST_TYPE_DXGI_BASE_FILTER            (gst_dxgi_base_filter_get_type())
#define GST_DXGI_BASE_FILTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DXGI_BASE_FILTER,GstDXGIBaseFilter))
#define GST_IS_DXGI_BASE_FILTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DXGI_BASE_FILTER))
#define GST_DXGI_BASE_FILTER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_DXGI_BASE_FILTER,GstDXGIBaseFilterClass))
#define GST_IS_DXGI_BASE_FILTER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_DXGI_BASE_FILTER))
#define GST_DXGI_BASE_FILTER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_DXGI_BASE_FILTER,GstDXGIBaseFilterClass))

/**
 * GstGLBaseFilter:
 * @display: the currently configured #GstGLDisplay
 * @context: the currently configured #GstGLContext
 * @in_caps: the currently configured input #GstCaps
 * @out_caps: the currently configured output #GstCaps
 *
 * The parent instance type of a base GStreamer GL Filter.
 */
struct _GstDXGIBaseFilter
{
  GstBaseTransform   parent;

  /*< public >*/
  GstDXGID3D11Context *context;

  GstCaps           *in_caps;
  GstCaps           *out_caps;

  /*< private >*/
  gpointer _padding[GST_PADDING];
};

/**
 * GstGLBaseFilterClass:
 * @supported_gl_api: the logical-OR of #GstGLAPI's supported by this element
 * @gl_start: called in the GL thread to setup the element GL state.
 * @gl_stop: called in the GL thread to setup the element GL state.
 * @gl_set_caps: called in the GL thread when caps are set on @filter.
 *
 * The base class for GStreamer GL Filter.
 */
struct _GstDXGIBaseFilterClass
{
  GstBaseTransformClass parent_class;

  /*< public >*/
  gboolean (*dxgi_start)          (GstGLBaseFilter *filter);
  void     (*dxgi_stop)           (GstGLBaseFilter *filter);
  gboolean (*dxgi_set_caps)       (GstGLBaseFilter *filter, GstCaps * incaps, GstCaps * outcaps);

  /*< private >*/
  gpointer _padding[GST_PADDING];
};

gboolean        gst_gl_base_filter_find_dxgi_context          (GstDXGIBaseFilter * filter);

G_END_DECLS

#endif /* _GST_DXGI_BASE_FILTER_H_ */
