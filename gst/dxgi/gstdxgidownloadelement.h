/*
 * Copyright (c) 2018 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#ifndef _GST_DXGI_DOWNLOAD_ELEMENT_H__
#define _GST_DXGI_DOWNLOAD_ELEMENT_H__

#include <gst/video/video.h>
#include <gst/gstmemory.h>

#include <gst/dxgi/dxgi.h>

G_BEGIN_DECLS

GType gst_dxgi_download_element_get_type (void);
#define GST_TYPE_DXGI_DOWNLOAD_ELEMENT (gst_dxgi_download_element_get_type())
#define GST_DXGI_DOWNLOAD_ELEMENT(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DXGI_DOWNLOAD_ELEMENT,GstDXGIDownloadElement))
#define GST_DXGI_DOWNLOAD_ELEMENT_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DXGI_DISPLAY,GstDXGIDownloadElementClass))
#define GST_IS_DXGI_DOWNLOAD_ELEMENT(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DXGI_DOWNLOAD_ELEMENT))
#define GST_IS_DXGI_DOWNLOAD_ELEMENT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DXGI_DOWNLOAD_ELEMENT))
#define GST_DXGI_DOWNLOAD_ELEMENT_CAST(obj) ((GstDXGIDownloadElement*)(obj))

typedef struct _GstDXGIDownloadElement GstDXGIDownloadElement;
typedef struct _GstDXGIDownloadElementClass GstDXGIDownloadElementClass;

struct _GstDXGIDownloadElement
{
  /* <private> */
  GstDXGIBaseFilter  parent;

  gboolean do_pbo_transfers;
  GstAllocator * dmabuf_allocator;
  gboolean add_videometa;
};

struct _GstDXGIDownloadElementClass
{
  /* <private> */
  GstBaseTransform object_class;
};

G_END_DECLS

#endif /* _GST_DXGI_DOWNLOAD_ELEMENT_H__ */
