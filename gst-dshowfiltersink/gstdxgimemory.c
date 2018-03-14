// vim: ts=2:sw=2
/* #include <D3D11.h> */
#include <d3d11.h>
#include <dxgi.h>
/* #include <d3d11_3.h> */

#include "gstdxgimemory.h"

#define GL_MEM_WIDTH(gl_mem) _get_plane_width (&gl_mem->info, gl_mem->plane)
#define GL_MEM_HEIGHT(gl_mem) _get_plane_height (&gl_mem->info, gl_mem->plane)
#define GL_MEM_STRIDE(gl_mem) GST_VIDEO_INFO_PLANE_STRIDE (&gl_mem->info, gl_mem->plane)

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

GST_DEBUG_CATEGORY_STATIC (GST_CAT_GL_MEMORY);
#define GST_CAT_DEFAULT GST_CAT_GL_MEMORY

#define parent_class gst_gl_dxgi_memory_allocator_parent_class

static void
gst_gl_dxgi_memory_allocator_dispose (GObject * object)
{
  GstGLDXGIMemoryAllocator *self = GST_GL_DXGI_MEMORY_ALLOCATOR (object);

  if (self->sink)
    gst_object_unref (self->sink);
  self->sink = NULL;

  G_OBJECT_CLASS (gst_gl_dxgi_memory_allocator_parent_class)->dispose (object);
}

/* static void */
/* gst_gl_dxgi_memory_allocator_free (GstAllocator * allocator, GstMemory * mem) */
/* { */
/*   GstGLDXGIMemory *mymem = (GstGLDXGIMemory *) mem; */

/*   if (mymem->block) { */
/*     GST_OBJECT_LOCK (mymem->sink); */

/*     //sp_writer_free_block (mymem->block); */
/*     GST_OBJECT_UNLOCK (mymem->sink); */
/*     gst_object_unref (mymem->sink); */
/*   } */
/*   gst_object_unref (mem->allocator); */

/*   g_slice_free (GstGLDXGIMemory, mymem); */
/* } */

/* static gpointer */
/* gst_gl_dxgi_memory_allocator_mem_map (GstMemory * mem, gsize maxsize, */
/*     GstMapFlags flags) */
/* { */
/*   GstGLDXGIMemory *mymem = (GstGLDXGIMemory *) mem; */

/*   return mymem->data; */
/* } */



/* static void */
/* gst_gl_dxgi_memory_allocator_mem_unmap (GstMemory * mem) */
/* { */
/* } */

/* static GstMemory * */
/* gst_gl_dxgi_allocator_mem_share (GstMemory * mem, gssize offset, gssize size) */
/* { */
/*   GstGLDXGIMemory *mymem = (GstGLDXGIMemory *) mem; */
/*   GstGLDXGIMemory *mysub; */
/*   GstMemory *parent; */

/*   /1* find the real parent *1/ */
/*   if ((parent = mem->parent) == NULL) */
/*     parent = mem; */

/*   if (size == -1) */
/*     size = mem->size - offset; */

/*   mysub = g_slice_new0 (GstGLDXGIMemory); */
/*   /1* the shared memory is always readonly *1/ */
/*   gst_memory_init (GST_MEMORY_CAST (mysub), GST_MINI_OBJECT_FLAGS (parent) | */
/*       GST_MINI_OBJECT_FLAG_LOCK_READONLY, gst_object_ref (mem->allocator), */
/*       parent, mem->maxsize, mem->align, mem->offset + offset, size); */
/*   mysub->data = mymem->data; */

/*   return (GstMemory *) mysub; */
/* } */

/* static gboolean */
/* gst_gl_dxgi_allocator_mem_is_span (GstMemory * mem1, GstMemory * mem2, */
/*     gsize * offset) */
/* { */

/*   DebugBreak(); */
/*   GstGLDXGIMemory *mymem1 = (GstGLDXGIMemory *) mem1; */
/*   GstGLDXGIMemory *mymem2 = (GstGLDXGIMemory *) mem2; */

/*   if (offset) { */
/*     GstMemory *parent; */

/*     parent = mem1->parent; */

/*     *offset = mem1->offset - parent->offset; */
/*   } */

/*   /1* and memory is contiguous *1/ */
/*   return mymem1->data + mem1->offset + mem1->size == */
/*       mymem2->data + mem2->offset; */
/* } */

static void
gst_gl_dxgi_memory_allocator_init (GstGLDXGIMemoryAllocator * self)
{

  GST_DEBUG_OBJECT(self, "-------> gst_gl_dxgi_allocator_init");
  GstAllocator *allocator = GST_ALLOCATOR_CAST (self);

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}


/* static GstMemory * */
/* gst_gl_dxgi_allocator_alloc_locked(GstGLDXGIMemoryAllocator * self, gsize size, */
/*   GstAllocationParams * params) */
/* { */
/*   GST_ERROR_OBJECT(self, */
/*     "gst_gl_dxgi_allocator_alloc_locked"); */

/* //FIXME */
/* #if 0 */
/*     GstMemory *memory = NULL; */

/*   gsize maxsize = size + params->prefix + params->padding; */
/*   gsize align = params->align; */

/*   /1* ensure configured alignment *1/ */
/*   align |= gst_memory_alignment; */
/*   /1* allocate more to compensate for alignment *1/ */
/*   maxsize += align; */

/*   GstGLDXGIMemory *mymem; */
/*   gsize aoffset, padding; */

/*   mymem = g_slice_new0(GstGLDXGIMemory); */
/*   memory = GST_MEMORY_CAST(mymem); */
/*   mymem->sink = gst_object_ref(self->sink); */


/*   uint64_t i = self->sink->shmem->write_ptr % self->sink->shmem->count; */
/*   self->sink->shmem->write_ptr++; */
/*   uint64_t frame_offset = self->sink->shmem->frame_offset + i * self->sink->shmem->frame_size; */
/*   uint64_t data_offset = self->sink->shmem->frame_data_offset; */

/*   struct frame_header *frame = ((struct frame_header*) (((unsigned char*)self->sink->shmem) + frame_offset)); */
/*   void *data = ((char*)frame) + data_offset; */
/*   mymem->data = data; */

/*   /1* do alignment *1/ */
/*   if ((aoffset = ((guintptr)mymem->data & align))) { */
/*     aoffset = (align + 1) - aoffset; */
/*     mymem->data += aoffset; */
/*     maxsize -= aoffset; */
/*   } */

/*   if (params->prefix && (params->flags & GST_MEMORY_FLAG_ZERO_PREFIXED)) */
/*     memset(mymem->data, 0, params->prefix); */

/*   padding = maxsize - (params->prefix + size); */
/*   if (padding && (params->flags & GST_MEMORY_FLAG_ZERO_PADDED)) */
/*     memset(mymem->data + params->prefix + size, 0, padding); */

/*   gst_memory_init(memory, params->flags, g_object_ref(self), NULL, */
/*     maxsize, align, params->prefix, size); */

/*   return memory; */
/* #endif */
/* } */

/* static GstMemory * */
/* gst_gl_dxgi_sink_allocator_alloc (GstAllocator * allocator, gsize size, */
/*     GstAllocationParams * params) */
/* { */
/*   GstGLDXGIMemoryAllocator *self = GST_GL_DXGI_MEMORY_ALLOCATOR (allocator); */
/*   GST_ERROR_OBJECT(self, "gst_gl_dxgi_sink_allocator_alloc - allocating %d", size); */

/*   /// FIXME - we can do this better */
/*   return GST_GL_DXGI_MEMORY_ALLOCATOR_CLASS(G_OBJECT_GET_CLASS(self))->orig_alloc(allocator, size, params); */

/*   /1* GstMemory *memory = NULL; *1/ */

/*   /1* GST_OBJECT_LOCK (self->sink); *1/ */
/*   /1* memory = gst_gl_dxgi_allocator_alloc_locked (self, size, params); *1/ */
/*   /1* GST_OBJECT_UNLOCK (self->sink); *1/ */

/*   /1* if (!memory) { *1/ */
/*   /1*   memory = gst_allocator_alloc(NULL, size, params); *1/ */
/*   /1*   GST_LOG_OBJECT(self, *1/ */
/*   /1*     "Not enough shared memory for GstMemory of %" G_GSIZE_FORMAT *1/ */
/*   /1*     "bytes, allocating using standard allocator", size); *1/ */
/*   /1* } *1/ */

/*   /1* return memory; *1/ */
/* } */
/* static GstGLDXGIMemory * _gl_mem_dshow_alloc (GstGLBaseMemoryAllocator * allocator,
 */
/*     GstGLVideoAllocationParams * params) {
 */
/* 
 */
/*   GstGLBaseMemoryAllocatorClass *alloc_class;
 */
/*   GST_ERROR_OBJECT(allocator, "_gl_mem_dshow_alloc - allocating"); */
/*   DebugBreak(); */
/*   GST_ERROR_OBJECT(allocator, __func__);
 */
/*   alloc_class = GST_GL_BASE_MEMORY_ALLOCATOR_CLASS (parent_class);
 */
/*   return alloc_class->alloc(allocator, params);
 */
/* 
 */
/* }
 */


static guint
_new_texture (GstGLContext * context, guint target, guint internal_format,
    guint format, guint type, guint width, guint height, HANDLE * interop_handle)
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

  DebugBreak();

  D3D11_TEXTURE2D_DESC desc = { 0 };
  desc.Width = width;
  desc.Height = height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = 0 ;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

  if (type == GL_UNSIGNED_BYTE && format == GST_GL_RGBA) {
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  } else {
    GST_ERROR("UNKNOWN FORMAT %#010x %#010x", type , format);
    return 0;
  }

  GstDXGID3D11Context *share_context = get_dxgi_share_context(context);

  ID3D11Texture2D *d3d11texture;
  HRESULT result = share_context->d3d11_device->lpVtbl->CreateTexture2D(share_context->d3d11_device,
      &desc, NULL, &d3d11texture);
  g_assert(result == S_OK);
  gl->GenTextures (1, &tex_id);

  *interop_handle = share_context->wglDXRegisterObjectNV(
      share_context->device_interop_handle,
      d3d11texture,
      tex_id,
      target,
      WGL_ACCESS_READ_WRITE_NV);

  GST_ERROR("wglDXRegisterObjectNV texture_id %#010x interop_id:%#010x",
    tex_id,
    *interop_handle);

  gl->BindTexture (target, tex_id);
  if (target == GL_TEXTURE_2D || target == GL_TEXTURE_RECTANGLE) {
    /* gl->TexImage2D (target, 0, internal_format, width, height, 0, format, type, */
    /*     NULL); */
  }

  gl->TexParameteri (target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->TexParameteri (target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->TexParameteri (target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->TexParameteri (target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  gl->BindTexture (target, 0);

  return tex_id;
}

static GstMemory *
_default_gl_dxgi_tex_copy (GstGLMemory * src, gssize offset, gssize size)
{
  GST_CAT_ERROR (GST_CAT_GL_MEMORY, "Cannot copy DXGI textures");
  return NULL;
}

static gboolean
_gl_dxgi_tex_create (GstGLDXGIMemory * gl_dxgi_mem, GError ** error)
{
  DebugBreak();
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
        GL_MEM_HEIGHT (gl_mem), &gl_dxgi_mem->interop_handle);

    GST_TRACE ("Generating texture id:%u format:%u type:%u dimensions:%ux%u",
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
_gl_dxgi_tex_map (GstGLDXGIMemory *gl_mem, GstMapInfo *info, gsize maxsize)
{

  if ((info->flags & GST_MAP_GL) == GST_MAP_GL) {
    GST_ERROR("wglDXLockObjectsNV texture_id %#010x interop_id:%#010x",
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
_gl_dxgi_tex_unmap (GstGLDXGIMemory * gl_mem, GstMapInfo * info)
{
  /// FIXME - I should propably first unmap and then unlock...
  GstGLMemoryAllocatorClass *alloc_class;
  alloc_class = GST_GL_MEMORY_ALLOCATOR_CLASS (parent_class);
  alloc_class->unmap ((GstGLBaseMemory *) gl_mem, info);
  if ((info->flags & GST_MAP_GL) == GST_MAP_GL) {
    GST_ERROR("wglDXUnlockObjectsNV texture_id %#010x interop_id:%#010x",
      gl_mem->mem.tex_id,
      gl_mem->interop_handle);
    GstGLContext *context = gl_mem->mem.mem.context;
    GstDXGID3D11Context *share_context = get_dxgi_share_context(context);
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
_gl_mem_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  g_warning ("Use gst_gl_base_memory_alloc () to allocate from this "
      "GstGLDXGIMemory allocator?");

  return NULL;
}

static GstGLDXGIMemory *
_gl_dxgi_mem_alloc (GstGLBaseMemoryAllocator * allocator,
    GstGLVideoAllocationParams * params)
{
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

static void
gst_gl_dxgi_memory_allocator_class_init (GstGLDXGIMemoryAllocatorClass * klass)
{


  GstGLBaseMemoryAllocatorClass *gl_base;
  GstGLMemoryAllocatorClass *gl_tex;
  gl_tex = (GstGLMemoryAllocatorClass *) klass;
  gl_base = (GstGLBaseMemoryAllocatorClass *) klass;
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);

  gl_tex->map = (GstGLBaseMemoryAllocatorMapFunction) _gl_dxgi_tex_map;
  gl_tex->unmap = (GstGLBaseMemoryAllocatorUnmapFunction) _gl_dxgi_tex_unmap;
  gl_tex->copy = (GstGLBaseMemoryAllocatorCopyFunction) _default_gl_dxgi_tex_copy;

  /* TODO: */
  /* gl_base->destroy = (GstGLBaseMemoryAllocatorDestroyFunction) _gl_mem_destroy; */

  gl_base->alloc = (GstGLBaseMemoryAllocatorAllocFunction) _gl_dxgi_mem_alloc;
  gl_base->create = (GstGLBaseMemoryAllocatorCreateFunction) _gl_dxgi_tex_create;
  gl_base->copy = (GstGLBaseMemoryAllocatorCopyFunction) _default_gl_dxgi_tex_copy;

  allocator_class->alloc = _gl_mem_alloc;

  GST_DEBUG_CATEGORY_INIT (GST_CAT_GL_MEMORY, "gldxgitexture", 0,
        "OpenGL DGXI shared memory");
}

GstGLDXGIMemoryAllocator *
gst_gl_dxgi_memory_allocator_new (GstBaseSink* sink)
{
  GstGLDXGIMemoryAllocator *self = g_object_new (GST_TYPE_GL_DXGI_MEMORY_ALLOCATOR, NULL);

  self->sink = gst_object_ref (sink);

  return self;
}

