/*
 * GStreamer
 * Copyright (C) <2018> Pigs in Flight, Inc (Bebo)
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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
 * 
 *  vim: ts=2:sw=2
 */
#ifndef __GST_DXGI_MEMORY_H__
#define __GST_DXGI_MEMORY_H__

#define COBJMACROS

#include <windows.h>
#include <d3d12.h>
#include <dxgi.h>

#include <gst/gst.h>
#define _GST_GL_MEMORY_PBO_H_ // FIXME - no idea why
#include <gst/gl/gstglmemory.h>

#include <gst/gl/gl.h>
#include <gst/gl/gstglfuncs.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>


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

typedef enum {
  GST_DXGI_GL_UNLOCKED,
  GST_DXGI_GL_LOCKED,
//  GST_DXGI_D3D_MAPPED,
} GLDXGILockStatus;

typedef struct _GstGLDXGIMemory
{
  GstGLMemory mem;
  HANDLE interop_handle;
  HANDLE dxgi_handle;
  ID3D12Resource *d3d_texture;
  GLDXGILockStatus status;
  gpointer    _padding[GST_PADDING];
} GstGLDXGIMemory;

typedef struct _GstGLDXGIMemoryAllocator
{
  GstGLMemoryAllocator parent;
  /* GstBaseSink *sink; */
} GstGLDXGIMemoryAllocator;

typedef struct _GstGLDXGIMemoryAllocatorClass
{
  GstGLMemoryAllocatorClass parent_class;
  GstGLBaseMemoryAllocatorAllocFunction orig_alloc;
} GstGLDXGIMemoryAllocatorClass;

void gl_dxgi_map_d3d (GstGLDXGIMemory * gl_mem);
void gl_dxgi_unmap_d3d (GstGLDXGIMemory * gl_mem);

GType gst_gl_dxgi_memory_allocator_get_type (void);
GstGLDXGIMemoryAllocator * gst_gl_dxgi_memory_allocator_new();

#endif /* __GST_DXGI_MEMORY_H__ */
