// vim: ts=2:sw=2
/* GStreamer
 * Copyright (C) <2009> Collabora Ltd
 *  @author: Olivier Crete <olivier.crete@collabora.co.uk
 * Copyright (C) <2009> Nokia Inc
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_SHM_SINK_H__
#define __GST_SHM_SINK_H__

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

#include <gst/gst.h>
#include <gst/base/gstbasesink.h>
#include "gstdxgimemory.h"

G_BEGIN_DECLS
#define GST_TYPE_SHM_SINK \
  (gst_shm_sink_get_type())
#define GST_SHM_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SHM_SINK,GstShmSink))
#define GST_SHM_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SHM_SINK,GstShmSinkClass))
#define GST_IS_SHM_SINK(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SHM_SINK))
#define GST_IS_SHM_SINK_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SHM_SINK))
typedef struct _GstShmSink GstShmSink;
typedef struct _GstShmSinkClass GstShmSinkClass;

struct _GstShmSink
{
  GstBaseSink element;
  GstGLContext *context;
  GstGLContext *other_context;
  GstGLDisplay *display;

  struct shmem *shmem;
  HANDLE shmem_handle;
  HANDLE shmem_mutex;
  HANDLE shmem_new_data_semaphore;

  gboolean wait_for_connection;
  gboolean stop;
  gboolean unlock;
  GstClockTime first_render_time;
  GstClockTimeDiff buffer_time;

  GCond cond;

  GstGLDXGIMemoryAllocator *allocator;
  GstAllocationParams params;
};

struct _GstShmSinkClass
{
  GstBaseSinkClass parent_class;
};

GType gst_shm_sink_get_type(void);






G_END_DECLS
#endif /* __GST_SHM_SINK_H__ */
