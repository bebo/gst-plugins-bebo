/* GStreamer
 * Copyright (C) <2018> Pigs in Flight, Inc (Bebo)
 * @author: Florian Nierhaus <fpn@bebo.com>
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
 * vim: ts=2:sw=2
 */

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include "gstdxgimemory.h"

#ifdef NDEBUG
#undef GST_LOG_OBJECT
#define GST_LOG_OBJECT(...)
#undef GST_LOG
#define GST_LOG(...)
#endif

#define GL_MEM_HEIGHT(gl_mem) _get_plane_height (&gl_mem->info, gl_mem->plane)
#define GL_DXGI_MEM_HEIGHT(gl_mem) _get_plane_height (&gl_mem->mem.info, gl_mem->mem.plane)

static inline guint
_get_plane_width (GstVideoInfo * info, guint plane)
{
  if (GST_VIDEO_INFO_IS_YUV (info))
    /* For now component width and plane width are the same and the
     * plane-component mapping matches
     */
    return GST_VIDEO_INFO_COMP_WIDTH (info, plane);
  else                          /* RGB, GRAY */
    return GST_VIDEO_INFO_WIDTH (info);
}

static inline guint
_get_plane_height (GstVideoInfo * info, guint plane)
{
  if (GST_VIDEO_INFO_IS_YUV (info))
    /* For now component width and plane width are the same and the
     * plane-component mapping matches
     */
    return GST_VIDEO_INFO_COMP_HEIGHT (info, plane);
  else                          /* RGB, GRAY */
    return GST_VIDEO_INFO_HEIGHT (info);
}

/********************
 * CUSTOM ALLOCATOR *
 ********************/
#define GST_GL_DXGI_ALLOCATOR_NAME "GstDXGIMemory"

G_DEFINE_TYPE(GstGLDXGIMemoryAllocator, gst_gl_dxgi_memory_allocator,
   GST_TYPE_GL_MEMORY_ALLOCATOR);

GST_DEBUG_CATEGORY_STATIC (GST_CAT_GL_DXGI_MEMORY);
#define GST_CAT_DEFAULT GST_CAT_GL_DXGI_MEMORY

#define parent_class gst_gl_dxgi_memory_allocator_parent_class

static void
gst_gl_dxgi_memory_allocator_init (GstGLDXGIMemoryAllocator * self)
{

  GST_DEBUG_OBJECT(self, "-------> gst_gl_dxgi_allocator_init");
  GstAllocator *allocator = GST_ALLOCATOR_CAST (self);

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static guint
_new_texture (GstGLContext * context, guint target, guint internal_format,
    guint format, guint type, guint width, guint height, HANDLE * interop_handle, ID3D11Texture2D ** d3d11texture, HANDLE * dxgi_handle)
{
  const GstGLFuncs *gl = context->gl_vtable;

  guint tex_id;

#ifndef G_DISABLE_ASSERT
  g_assert(target == GL_TEXTURE_2D
                  || target == GL_TEXTURE_2D_ARRAY
                  || target == GL_TEXTURE_3D
                  || target == GL_TEXTURE_CUBE_MAP
                  || target == GL_TEXTURE_RECTANGLE
                  || target == GL_RENDERBUFFER);
#endif

  D3D11_TEXTURE2D_DESC desc = { 0 };
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
  desc.CPUAccessFlags = 0;
  desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

  if (type == GL_UNSIGNED_BYTE && format == GST_GL_RGBA) {
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  } else {
    GST_ERROR("UNKNOWN FORMAT %#010x %#010x", type , format);
    return 0;
  }

  GstDXGID3D11Context *share_context = get_dxgi_share_context(context);

  HRESULT result = share_context->d3d11_device->lpVtbl->CreateTexture2D(share_context->d3d11_device,
      &desc, NULL, d3d11texture);
  g_assert(result == S_OK);

  gl->GenTextures(1, &tex_id);

  *interop_handle = share_context->wglDXRegisterObjectNV(
      share_context->device_interop_handle,
      *d3d11texture,
      tex_id,
      target,
      WGL_ACCESS_READ_WRITE_NV);


  IDXGIResource *dxgi_res;
  GUID myIID_IDXGIResource = {
    0x035f3ab4, 0x482e, 0x4e50, { 0xb4, 0x1f, 0x8a, 0x7f, 0x8b, 0xd8, 0x96, 0x0b}};
  HRESULT hr = (*d3d11texture)->lpVtbl->QueryInterface(*d3d11texture, &myIID_IDXGIResource,
      (void**)&dxgi_res);
  if (FAILED(hr)) {
    GST_ERROR("failed to query IDXGIResource interface %#010x", hr);
    return 0;
  }

  hr = dxgi_res->lpVtbl->GetSharedHandle(dxgi_res, dxgi_handle);
  dxgi_res->lpVtbl->Release(dxgi_res);
  if (FAILED(hr)) {
    GST_ERROR("failed to get shared handle %#010x", hr);
    return 0;
  }

  gl->BindTexture (target, tex_id);
  gl->TexParameteri (target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->TexParameteri (target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->TexParameteri (target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->TexParameteri (target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  gl->BindTexture (target, 0);

  GST_DEBUG("new dxgi texture texture_id %#010x interop_id: %#010x dxgi_handle: %llu %dx%d",
    tex_id,
    *interop_handle,
    *dxgi_handle,
    width,
    height);

  return tex_id;
}

/* static GstMemory * */
/* _default_gl_dxgi_tex_copy (GstGLMemory * src, gssize offset, gssize size) */
/* { */
/*   GST_CAT_ERROR (GST_CAT_GL_DXGI_MEMORY, "Cannot copy DXGI textures"); */
/*   return NULL; */
/* } */

static gboolean
gl_dxgi_tex_create (GstGLDXGIMemory * gl_dxgi_mem, GError ** error)
{
  GST_DEBUG("gl_dxgi_tex_create");
  GstGLMemory * gl_mem = (GstGLMemory *)gl_dxgi_mem;
  GstGLContext *context = gl_mem->mem.context;
  GLenum internal_format;
  GLenum tex_format;
  GLenum tex_type;

  tex_format = gl_mem->tex_format;
  tex_type = GL_UNSIGNED_BYTE;
  if (gl_mem->tex_format == GST_GL_RGB565) {
    tex_format = GST_GL_RGB;
    tex_type = GL_UNSIGNED_SHORT_5_6_5;
  }

  internal_format =
      gst_gl_sized_gl_format_from_gl_format_type (context, tex_format,
      tex_type);

  if (!gl_mem->texture_wrapped) {
    gl_mem->tex_id =
        _new_texture (context, gst_gl_texture_target_to_gl (gl_mem->tex_target),
        internal_format, tex_format, tex_type, gl_mem->tex_width,
        GL_MEM_HEIGHT (gl_mem),
        &gl_dxgi_mem->interop_handle,
        &gl_dxgi_mem->d3d11texture,
        &gl_dxgi_mem->dxgi_handle);

    GST_DEBUG("Generating texture id:%u format:%u type:%u dimensions:%ux%u",
        gl_mem->tex_id, tex_format, tex_type, gl_mem->tex_width,
        GL_MEM_HEIGHT (gl_mem));
  }

  return TRUE;
}

GstDXGID3D11Context * get_dxgi_share_context(GstGLContext * context) {
  GstDXGID3D11Context *share_context;
  share_context = (GstDXGID3D11Context*) g_object_get_data((GObject*) context, GST_GL_DXGI_D3D11_CONTEXT);
  return share_context;
}

static gpointer
gl_dxgi_tex_map (GstGLDXGIMemory *gl_mem, GstMapInfo *info, gsize maxsize)
{
  if ((info->flags & GST_MAP_GL) == GST_MAP_GL) {
    GST_LOG("wglDXLockObjectsNV texture_id %#010x interop_id:%#010x",
      gl_mem->mem.tex_id,
      gl_mem->interop_handle);

    GstGLContext *context = gl_mem->mem.mem.context;
    GstDXGID3D11Context *share_context = get_dxgi_share_context(context);
    BOOL result = share_context->wglDXLockObjectsNV(share_context->device_interop_handle,
                                      1,
                                      &gl_mem->interop_handle);
    if (result == FALSE) {
      DWORD error = GetLastError();
      if (error == ERROR_BUSY) {
        GST_ERROR("wglDXLockObjectsNV FAILED ERROR_BUSY texture_id %#010x interop_id:%#010x",
          gl_mem->mem.tex_id,
          gl_mem->interop_handle);
      } else if (error == ERROR_INVALID_DATA) {
        GST_ERROR("wglDXLockObjectsNV FAILED ERROR_INVALID_DATA texture_id %#010x interop_id:%#010x",
          gl_mem->mem.tex_id,
          gl_mem->interop_handle);
      } else {
        GST_ERROR("wglDXLockObjectsNV FAILED UNKNOWN ERROR %#010x texture_id %#010x interop_id:%#010x",
          error,
          gl_mem->mem.tex_id,
          gl_mem->interop_handle);
      }
    }
  }

  GstGLMemoryAllocatorClass *alloc_class;
  alloc_class = GST_GL_MEMORY_ALLOCATOR_CLASS (parent_class);
  return alloc_class->map ((GstGLBaseMemory *) gl_mem, info, maxsize);
}

static void
gl_dxgi_tex_unmap (GstGLDXGIMemory * gl_mem, GstMapInfo * info)
{
  GstGLMemoryAllocatorClass *alloc_class;
  alloc_class = GST_GL_MEMORY_ALLOCATOR_CLASS (parent_class);
  alloc_class->unmap ((GstGLBaseMemory *) gl_mem, info);

  if ((info->flags & GST_MAP_GL) == GST_MAP_GL) {
    GST_LOG("wglDXUnlockObjectsNV texture_id %#010x interop_id:%#010x",
      gl_mem->mem.tex_id,
      gl_mem->interop_handle);
    GstGLContext *context = gl_mem->mem.mem.context;
    GstDXGID3D11Context *share_context = get_dxgi_share_context(context);
    const GstGLFuncs *gl = context->gl_vtable;

    // gl->Flush();
    BOOL result = share_context->wglDXUnlockObjectsNV(share_context->device_interop_handle,
                                        1,
                                        &gl_mem->interop_handle);

    if (result == FALSE) {
      DWORD error = GetLastError();
      if (error == ERROR_NOT_LOCKED) {
        GST_ERROR("wglDXUnlockObjectsNV FAILED ERROR_NOT_LOCKED texture_id %#010x interop_id:%#010x",
          gl_mem->mem.tex_id,
          gl_mem->interop_handle);
      } else if (error == ERROR_INVALID_DATA) {
        GST_ERROR("wglDXUnlockObjectsNV FAILED ERROR_INVALID_DATA texture_id %#010x interop_id:%#010x",
          gl_mem->mem.tex_id,
          gl_mem->interop_handle);
      } else if (error == ERROR_LOCK_FAILED)  {
        GST_ERROR("wglDXUnlockObjectsNV FAILED ERROR_LOCK_FAILED texture_id %#010x interop_id:%#010x",
          gl_mem->mem.tex_id,
          gl_mem->interop_handle);
      } else {
        GST_ERROR("wglDXUnlockObjectsNV FAILED UNKNOWN ERROR %#010x texture_id %#010x interop_id:%#010x",
          error,
          gl_mem->mem.tex_id,
          gl_mem->interop_handle);
      }
    }
  }
}

static GstMemory *
gl_mem_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_warning ("Use gst_gl_base_memory_alloc () to allocate from this "
      "GstGLDXGIMemory allocator?");

  return NULL;
}

static GstGLDXGIMemory *
gl_dxgi_mem_alloc (GstGLBaseMemoryAllocator * allocator,
    GstGLVideoAllocationParams * params)
{
  GST_LOG("gl_dxgi_mem_alloc");
  GstGLDXGIMemory *mem;
  guint alloc_flags;

  alloc_flags = params->parent.alloc_flags;

  g_return_val_if_fail (alloc_flags & GST_GL_ALLOCATION_PARAMS_ALLOC_FLAG_VIDEO,
      NULL);

  mem = g_new0 (GstGLDXGIMemory, 1);

  if (alloc_flags & GST_GL_ALLOCATION_PARAMS_ALLOC_FLAG_WRAP_GPU_HANDLE) {
    mem->mem.tex_id = GPOINTER_TO_UINT (params->parent.gl_handle);
    mem->mem.texture_wrapped = TRUE;
  }

  gst_gl_memory_init (&mem->mem, GST_ALLOCATOR_CAST (allocator), NULL,
      params->parent.context, params->target, params->tex_format,
      params->parent.alloc_params, params->v_info, params->plane,
      params->valign, params->parent.user_data, params->parent.notify);

  if (alloc_flags & GST_GL_ALLOCATION_PARAMS_ALLOC_FLAG_WRAP_GPU_HANDLE) {
    GST_MINI_OBJECT_FLAG_SET (&mem->mem, GST_GL_BASE_MEMORY_TRANSFER_NEED_DOWNLOAD);
  }

  if (alloc_flags & GST_GL_ALLOCATION_PARAMS_ALLOC_FLAG_WRAP_SYSMEM) {
    GST_ERROR("Dont think GST_GL_ALLOCATION_PARAMS_ALLOC_FLAG_WRAP_SYSMEM will work");
    mem->mem.mem.data = params->parent.wrapped_data;
    GST_MINI_OBJECT_FLAG_SET (&mem->mem, GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD);
  }

  return mem;
}

static GstMemory *
gl_dxgi_text_copy (GstGLDXGIMemory * src, gssize offset, gssize size)
{
  GST_DEBUG("gl_dxgi_text_copy texture id:%u dimensions:%ux%u",
        src->mem.tex_id, src->mem.tex_width, GL_DXGI_MEM_HEIGHT (src));

  GstAllocationParams params = { 0, GST_MEMORY_CAST (src)->align, 0, 0 };
  GstGLBaseMemoryAllocator *base_mem_allocator;
  GstAllocator *allocator;
  GstGLDXGIMemory *dest = NULL;

  allocator = GST_MEMORY_CAST (src)->allocator;
  base_mem_allocator = (GstGLBaseMemoryAllocator *) allocator;

  if (src->mem.tex_target == GST_GL_TEXTURE_TARGET_EXTERNAL_OES) {
    GST_CAT_ERROR (GST_CAT_GL_DXGI_MEMORY, "Cannot copy External OES textures");
    return NULL;
  }

  /* If not doing a full copy, then copy to sysmem, the 2D represention of the
   * texture would become wrong */
  gsize s = size;
  if (offset > 0 || s < GST_MEMORY_CAST (src)->size) {
    return base_mem_allocator->fallback_mem_copy (GST_MEMORY_CAST (src), offset,
        s);
  }

  dest = g_new0 (GstGLDXGIMemory, 1);

  gst_gl_memory_init (GST_GL_MEMORY_CAST (dest), allocator, NULL,
      src->mem.mem.context, src->mem.tex_target, src->mem.tex_format, &params,
      &src->mem.info, src->mem.plane, &src->mem.valign, NULL, NULL);

  if (!GST_MEMORY_FLAG_IS_SET (src, GST_GL_BASE_MEMORY_TRANSFER_NEED_UPLOAD)) {
    GstMapInfo dinfo;

    if (!gst_memory_map (GST_MEMORY_CAST (dest), &dinfo,
            GST_MAP_WRITE | GST_MAP_GL)) {
      GST_CAT_WARNING (GST_CAT_GL_DXGI_MEMORY,
          "Failed not map destination for writing");
      gst_memory_unref (GST_MEMORY_CAST (dest));
      return NULL;
    }

    if (!gst_gl_memory_copy_into ((GstGLMemory *) src,
            ((GstGLMemory *) dest)->tex_id, src->mem.tex_target,
            src->mem.tex_format, src->mem.tex_width, GL_DXGI_MEM_HEIGHT (src))) {
      GST_CAT_WARNING (GST_CAT_GL_DXGI_MEMORY, "Could not copy GL Memory");
      gst_memory_unmap (GST_MEMORY_CAST (dest), &dinfo);
      goto memcpy;
    }

    gst_memory_unmap (GST_MEMORY_CAST (dest), &dinfo);
  } else {
  memcpy:
    if (!gst_gl_base_memory_memcpy ((GstGLBaseMemory *) src,
            (GstGLBaseMemory *) dest, offset, size)) {
      GST_CAT_WARNING (GST_CAT_GL_DXGI_MEMORY, "Could not copy GL Memory");
      gst_memory_unref (GST_MEMORY_CAST (dest));
      return NULL;
    }
  }

  return GST_MEMORY_CAST (dest);
}

static void
gl_mem_destroy (GstGLDXGIMemory * gl_mem)
{
  GST_DEBUG("gl_mem_destroy texture id:%u dimensions:%ux%u",
        gl_mem->mem.tex_id, gl_mem->mem.tex_width, GL_DXGI_MEM_HEIGHT (gl_mem));

  if (gl_mem->interop_handle) {
    GstGLContext *context = gl_mem->mem.mem.context;
    GstDXGID3D11Context *share_context = get_dxgi_share_context(context);
    BOOL result = share_context->wglDXUnregisterObjectNV(
            share_context->device_interop_handle,
            gl_mem->interop_handle);
    if (result == FALSE) {
      DWORD error = GetLastError();
      if (error == ERROR_BUSY) {
        GST_ERROR("wglDXUnregisterObjectNV FAILED ERROR_BUSY texture_id %#010x interop_id:%#010x",
          gl_mem->mem.tex_id,
          gl_mem->interop_handle);
      } else {
        GST_ERROR("wglDXUnregisterObjectNV FAILED UNKNOWN ERROR %#010x texture_id %#010x interop_id:%#010x",
          error,
          gl_mem->mem.tex_id,
          gl_mem->interop_handle);
      }
    }
    gl_mem->interop_handle = NULL;
  }
  if (gl_mem->d3d11texture) {
    gl_mem->d3d11texture->lpVtbl->Release(gl_mem->d3d11texture);
    gl_mem->d3d11texture = NULL;
    gl_mem->dxgi_handle = NULL;
  }

  GST_GL_BASE_MEMORY_ALLOCATOR_CLASS (parent_class)->destroy ((GstGLBaseMemory
          *) gl_mem);
}

static void
gst_gl_dxgi_memory_allocator_class_init (GstGLDXGIMemoryAllocatorClass * klass)
{

  GstGLBaseMemoryAllocatorClass *gl_base;
  GstGLMemoryAllocatorClass *gl_tex;
  gl_tex = (GstGLMemoryAllocatorClass *) klass;
  gl_base = (GstGLBaseMemoryAllocatorClass *) klass;
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);

  gl_base->alloc = (GstGLBaseMemoryAllocatorAllocFunction) gl_dxgi_mem_alloc;
  gl_base->create = (GstGLBaseMemoryAllocatorCreateFunction) gl_dxgi_tex_create;
  gl_base->destroy = (GstGLBaseMemoryAllocatorDestroyFunction) gl_mem_destroy;

  gl_tex->map = (GstGLBaseMemoryAllocatorMapFunction) gl_dxgi_tex_map;
  gl_tex->unmap = (GstGLBaseMemoryAllocatorUnmapFunction) gl_dxgi_tex_unmap;
  gl_tex->copy = (GstGLBaseMemoryAllocatorCopyFunction) gl_dxgi_text_copy;

  allocator_class->alloc = gl_mem_alloc;

  GST_DEBUG_CATEGORY_INIT (GST_CAT_GL_DXGI_MEMORY, "gldxgitexture", 0,
        "OpenGL DGXI shared memory");
}

GstGLDXGIMemoryAllocator *
gst_gl_dxgi_memory_allocator_new ()
{
  GstGLDXGIMemoryAllocator *self = g_object_new (GST_TYPE_GL_DXGI_MEMORY_ALLOCATOR, NULL);

  /* self->sink = gst_object_ref (sink); */

  return self;
}
