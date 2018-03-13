// vim: ts=2:sw=2
#include "gstdxgimemory.h"


/********************
 * CUSTOM ALLOCATOR *
 ********************/
#define GST_GL_DXGI_ALLOCATOR_NAME "GstDXGIMemory"

G_DEFINE_TYPE(GstGLDXGIMemoryAllocator, gst_gl_dxgi_memory_allocator,
   GST_TYPE_GL_MEMORY_ALLOCATOR);


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

static void
gst_gl_dxgi_memory_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  GstGLDXGIMemory *mymem = (GstGLDXGIMemory *) mem;

  if (mymem->block) {
    GST_OBJECT_LOCK (mymem->sink);

    //sp_writer_free_block (mymem->block);
    GST_OBJECT_UNLOCK (mymem->sink);
    gst_object_unref (mymem->sink);
  }
  gst_object_unref (mem->allocator);

  g_slice_free (GstGLDXGIMemory, mymem);
}

static gpointer
gst_gl_dxgi_memory_allocator_mem_map (GstMemory * mem, gsize maxsize,
    GstMapFlags flags)
{
  GstGLDXGIMemory *mymem = (GstGLDXGIMemory *) mem;

  return mymem->data;
}



static void
gst_gl_dxgi_memory_allocator_mem_unmap (GstMemory * mem)
{
}

static GstMemory *
gst_gl_dxgi_allocator_mem_share (GstMemory * mem, gssize offset, gssize size)
{
  GstGLDXGIMemory *mymem = (GstGLDXGIMemory *) mem;
  GstGLDXGIMemory *mysub;
  GstMemory *parent;

  /* find the real parent */
  if ((parent = mem->parent) == NULL)
    parent = mem;

  if (size == -1)
    size = mem->size - offset;

  mysub = g_slice_new0 (GstGLDXGIMemory);
  /* the shared memory is always readonly */
  gst_memory_init (GST_MEMORY_CAST (mysub), GST_MINI_OBJECT_FLAGS (parent) |
      GST_MINI_OBJECT_FLAG_LOCK_READONLY, gst_object_ref (mem->allocator),
      parent, mem->maxsize, mem->align, mem->offset + offset, size);
  mysub->data = mymem->data;

  return (GstMemory *) mysub;
}

static gboolean
gst_gl_dxgi_allocator_mem_is_span (GstMemory * mem1, GstMemory * mem2,
    gsize * offset)
{

  //DebugBreak();
  GstGLDXGIMemory *mymem1 = (GstGLDXGIMemory *) mem1;
  GstGLDXGIMemory *mymem2 = (GstGLDXGIMemory *) mem2;

  if (offset) {
    GstMemory *parent;

    parent = mem1->parent;

    *offset = mem1->offset - parent->offset;
  }

  /* and memory is contiguous */
  return mymem1->data + mem1->offset + mem1->size ==
      mymem2->data + mem2->offset;
}

static void
gst_gl_dxgi_memory_allocator_init (GstGLDXGIMemoryAllocator * self)
{

  GST_DEBUG_OBJECT(self, "-------> gst_gl_dxgi_allocator_init");
  GstAllocator *allocator = GST_ALLOCATOR_CAST (self);

  //  A_CLASS (b_parent_class)->method_to_call (obj, some_param);


  /* allocator->mem_map = gst_gl_dxgi_allocator_mem_map; */
  /* allocator->mem_unmap = gst_gl_dxgi_allocator_mem_unmap; */
  /* allocator->mem_share = gst_gl_dxgi_allocator_mem_share; */
  /* allocator->mem_is_span = gst_gl_dxgi_allocator_mem_is_span; */
  /* allocator->mem_type = GST_GL_MEMORY_ALLOCATOR_NAME; *1/ */

  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}


static GstMemory *
gst_gl_dxgi_allocator_alloc_locked(GstGLDXGIMemoryAllocator * self, gsize size,
  GstAllocationParams * params)
{
  GST_ERROR_OBJECT(self,
    "gst_gl_dxgi_allocator_alloc_locked");

//FIXME
#if 0
    GstMemory *memory = NULL;

  gsize maxsize = size + params->prefix + params->padding;
  gsize align = params->align;

  /* ensure configured alignment */
  align |= gst_memory_alignment;
  /* allocate more to compensate for alignment */
  maxsize += align;

  GstGLDXGIMemory *mymem;
  gsize aoffset, padding;

  mymem = g_slice_new0(GstGLDXGIMemory);
  memory = GST_MEMORY_CAST(mymem);
  mymem->sink = gst_object_ref(self->sink);


  uint64_t i = self->sink->shmem->write_ptr % self->sink->shmem->count;
  self->sink->shmem->write_ptr++;
  uint64_t frame_offset = self->sink->shmem->frame_offset + i * self->sink->shmem->frame_size;
  uint64_t data_offset = self->sink->shmem->frame_data_offset;

  struct frame_header *frame = ((struct frame_header*) (((unsigned char*)self->sink->shmem) + frame_offset));
  void *data = ((char*)frame) + data_offset;
  mymem->data = data;

  /* do alignment */
  if ((aoffset = ((guintptr)mymem->data & align))) {
    aoffset = (align + 1) - aoffset;
    mymem->data += aoffset;
    maxsize -= aoffset;
  }

  if (params->prefix && (params->flags & GST_MEMORY_FLAG_ZERO_PREFIXED))
    memset(mymem->data, 0, params->prefix);

  padding = maxsize - (params->prefix + size);
  if (padding && (params->flags & GST_MEMORY_FLAG_ZERO_PADDED))
    memset(mymem->data + params->prefix + size, 0, padding);

  gst_memory_init(memory, params->flags, g_object_ref(self), NULL,
    maxsize, align, params->prefix, size);

  return memory;
#endif
}

static GstMemory *
gst_gl_dxgi_sink_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{

  GstGLDXGIMemoryAllocator *self = GST_GL_DXGI_MEMORY_ALLOCATOR (allocator);
  GstMemory *memory = NULL;

  GST_OBJECT_LOCK (self->sink);
  memory = gst_gl_dxgi_allocator_alloc_locked (self, size, params);
  GST_OBJECT_UNLOCK (self->sink);

  if (!memory) {
    memory = gst_allocator_alloc(NULL, size, params);
    GST_LOG_OBJECT(self,
      "Not enough shared memory for GstMemory of %" G_GSIZE_FORMAT
      "bytes, allocating using standard allocator", size);
  }

  return memory;
}
static GstGLDXGIMemory * _gl_mem_dshow_alloc (GstGLBaseMemoryAllocator * allocator,
    GstGLVideoAllocationParams * params) {

  GstGLBaseMemoryAllocatorClass *alloc_class;
  GST_ERROR_OBJECT(allocator, __func__);
  alloc_class = GST_GL_BASE_MEMORY_ALLOCATOR_CLASS (parent_class);
  return alloc_class->alloc(allocator, params);

}

static gboolean _gl_dshow_tex_create(GstGLMemory * gl_mem, GError ** error) {
  GstGLBaseMemoryAllocatorClass *alloc_class;
  GST_ERROR_OBJECT(gl_mem, __func__);
  alloc_class = GST_GL_BASE_MEMORY_ALLOCATOR_CLASS (parent_class);
  return alloc_class->create(gl_mem, error);
}

static void
gst_gl_dxgi_memory_allocator_class_init (GstGLDXGIMemoryAllocatorClass * klass)
{

  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  GstGLBaseMemoryAllocatorClass *gl_base;
  GstGLMemoryAllocatorClass *gl_tex;
  /* GstAllocatorClass *allocator_class; */

  /* gl_tex = (GstGLMemoryAllocatorClass *) klass; */
  gl_base = (GstGLBaseMemoryAllocatorClass *) klass;
  /* allocator_class = (GstAllocatorClass *) klass; */

  gl_base->alloc = (GstGLBaseMemoryAllocatorAllocFunction) _gl_mem_dshow_alloc;
  gl_base->create = (GstGLBaseMemoryAllocatorCreateFunction) _gl_dshow_tex_create;

  /* allocator_class->alloc = gst_gl_dxgi_sink_allocator_alloc; */
  /* allocator_class->free = gst_gl_dxgi_allocator_free; */
  /* object_class->dispose = gst_gl_dxgi_allocator_dispose; */

}

GstGLDXGIMemoryAllocator *
gst_gl_dxgi_memory_allocator_new (GstBaseSink* sink)
{
  //DebugBreak();
  GstGLDXGIMemoryAllocator *self = g_object_new (GST_TYPE_GL_DXGI_MEMORY_ALLOCATOR, NULL);

  self->sink = gst_object_ref (sink);

  return self;
}

