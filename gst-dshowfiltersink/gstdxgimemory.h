// vim: ts=2:sw=2
#pragma once

#include <gst/gst.h>
/* #include <gst/gstallocator.h> */
/* #include <gst/gstmemory.h> */
/* #include <gst/video/video.h> */
#define _GST_GL_MEMORY_PBO_H_ // FIXME - no idea why
#include <gst/gl/gstglmemory.h>

//#include "gstdshowsink.h"

#define GST_IS_GL_DXGI_MEMORY_ALLOCATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_DXGI_MEMORY_ALLOCATOR))
#define GST_IS_GL_DXGI_MEMORY_ALLOCATOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_GL_DXGI_MEMORY_ALLOCATOR))

#define GST_TYPE_GL_DXGI_MEMORY_ALLOCATOR \
  (gst_gl_dxgi_memory_allocator_get_type())
#define GST_GL_DXGI_MEMORY_ALLOCATOR(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_DXGI_MEMORY_ALLOCATOR, \
      GstGLDXGIMemoryAllocator))
#define GST_GL_DXGI_MEMORY_ALLOCATOR_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_GL_DXGI_MEMORY_ALLOCATOR, \
      GstGLDXGIMemoryAllocatorClass))

typedef struct _GstGLDXGIMemory
{
  GstGLMemory mem;
  gchar *data;
  GstBaseSink *sink;
  struct shmem * block;
} GstGLDXGIMemory;

typedef struct _GstGLDXGIMemoryAllocator
{
  GstGLMemoryAllocator parent;
  GstBaseSink *sink;
} GstGLDXGIMemoryAllocator;

typedef struct _GstGLDXGIMemoryAllocatorClass
{
  GstGLMemoryAllocatorClass parent_class;
} GstGLDXGIMemoryAllocatorClass;

GType gst_gl_dxgi_memory_allocator_get_type(void);
GstGLDXGIMemoryAllocator * gst_gl_dxgi_memory_allocator_new(GstBaseSink* sink);
