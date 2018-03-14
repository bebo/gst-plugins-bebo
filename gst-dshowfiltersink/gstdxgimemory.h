// vim: ts=2:sw=2
#pragma once

#include <gst/gst.h>
#define _GST_GL_MEMORY_PBO_H_ // FIXME - no idea why
#include <gst/gl/gstglmemory.h>

#include <gst/gl/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>


/* #define GST_GL_DXGI_D3D11_DEVICE "gst_gl_dxgi_d3d11_device" */
#define GST_GL_DXGI_D3D11_CONTEXT "gst_gl_dxgi_d3d11_context"
/* #define GST_GL_DXGI_D3D11_HANDLE "gst_gl_dxgi_d3d11_handle" */

G_BEGIN_DECLS
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

typedef struct _GstDXGID3D11Context
{
  ID3D11Device * d3d11_device;
  HANDLE device_interop_handle;
  PFNWGLDXOPENDEVICENVPROC wglDXOpenDeviceNV;
  PFNWGLDXCLOSEDEVICENVPROC wglDXCloseDeviceNV;
  PFNWGLDXREGISTEROBJECTNVPROC wglDXRegisterObjectNV;
  PFNWGLDXUNREGISTEROBJECTNVPROC wglDXUnregisterObjectNV;
  PFNWGLDXLOCKOBJECTSNVPROC wglDXLockObjectsNV;
  PFNWGLDXUNLOCKOBJECTSNVPROC wglDXUnlockObjectsNV;
  PFNWGLDXSETRESOURCESHAREHANDLENVPROC wglDXSetResourceShareHandleNV;
} GstDXGID3D11Context;

typedef struct _GstGLDXGIMemory
{
  GstGLMemory mem;
  HANDLE      interop_handle;
  gpointer    _padding[GST_PADDING];
} GstGLDXGIMemory;

typedef struct _GstGLDXGIMemoryAllocator
{
  GstGLMemoryAllocator parent;
  GstBaseSink *sink;
} GstGLDXGIMemoryAllocator;

typedef struct _GstGLDXGIMemoryAllocatorClass
{
  GstGLMemoryAllocatorClass parent_class;
  GstGLBaseMemoryAllocatorAllocFunction orig_alloc;
} GstGLDXGIMemoryAllocatorClass;

GType gst_gl_dxgi_memory_allocator_get_type(void);
GstGLDXGIMemoryAllocator * gst_gl_dxgi_memory_allocator_new(GstBaseSink* sink);

GstDXGID3D11Context * get_dxgi_share_context(GstGLContext * context);
