/*
 * Copyright (c) 2019 Pigs in Flight, Inc.
 * Author: Jake Loo <jake@bebo.com>
 */

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <queue>
#include <sstream>

#include "ppapi/cpp/graphics_3d.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/var.h"
#include "ppapi/cpp/var_array.h"
#include "ppapi/cpp/var_dictionary.h"
#include "ppapi/lib/gl/gles2/gl2ext_ppapi.h"
#include "ppapi/utility/completion_callback_factory.h"
#include "shared/bebo_shmem.h"
#include "lru_cache.h"

#ifdef WIN32
#undef PostMessage
// Allow 'this' in initializer list
#pragma warning(disable : 4355)
#endif

#define error(format, ...) PostLogMessage("ERROR", format, __VA_ARGS__)
#define warn(format, ...)  PostLogMessage("WARNING", format, __VA_ARGS__)
#define info(format, ...)  PostLogMessage("INFO", format, __VA_ARGS__)
#define debug(format, ...) PostLogMessage("DEBUG", format, __VA_ARGS__)
#define log(format, ...)   PostLogMessage("LOG", format, __VA_ARGS__)

#define WAIT_MIN_NUM_OF_FRAMES_TO_UNREF 3
#define TEXTURE_CACHE_SIZE  64

namespace {

const char kFragShaderSource[] =
    "precision mediump float;\n"
    "varying vec3 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D u_texture;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(u_texture, v_texcoord);\n"
    "  gl_FragColor += vec4(v_color, 1);\n"
    "}\n";

const char kVertexShaderSource[] =
    "attribute vec2 a_texcoord;\n"
    "attribute vec3 a_color;\n"
    "attribute vec4 a_position;\n"
    "varying vec3 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "  gl_Position =  a_position;\n"
    "  v_color = a_color;\n"
    "  v_texcoord = a_texcoord;\n"
    "}\n";

struct Vertex {
  float loc[3];
  float color[3];
  float tex[2];
};

const Vertex  kBoxVerts[4] = {
  {{-1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
  {{+1.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
  {{-1.0f, +1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},
  {{+1.0f, +1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
};

const GLubyte kBoxIndexes[6] = {
  0, 1, 2, 1, 3, 2
};

}  // namespace

// Only to be used in thread that has access to GL.
class GLTextureFrame {
  public:
    GLTextureFrame(GLuint texture, GLuint64 surface):
      texture_(texture), surface_(surface) {}
    ~GLTextureFrame() {
      if (texture_) {
        glDeleteTextures(1, &texture_);
      }
      if (surface_) {
        glDestroySurfaceEGL(surface_);
      }
    }

    GLuint texture() const { return texture_; }
    GLuint64 surface() const { return surface_; }

  private:
    GLuint texture_;
    GLuint64 surface_;
};

class PreviewFrame {
  public:
    PreviewFrame(uint64_t nr, uint64_t ptr, uint64_t shared_handle):
      nr_(nr), ptr_(ptr), shared_handle_(shared_handle), texture_(0) {};

    uint64_t nr() const { return nr_; }
    uint64_t ptr() const { return ptr_; }
    uint64_t shared_handle() const { return shared_handle_; }
    GLuint texture() const { return texture_; }

    void SetTexture(GLuint texture) {
      texture_ = texture;
    }

  private:
    uint64_t ptr_;
    uint64_t nr_;
    uint64_t shared_handle_;
    GLuint texture_;
};

class PreviewInstance : public pp::Instance {
 public:
  explicit PreviewInstance(PP_Instance instance)
      : pp::Instance(instance),
        callback_factory_(this),
        width_(0),
        height_(0),
        negotiated_width_(0),
        negotiated_height_(0),
        frag_shader_(0),
        vertex_shader_(0),
        program_(0),
        texture_loc_(0),
        position_loc_(0),
        color_loc_(0),
        shmem_(NULL),
        shmem_handle_(NULL),
        shmem_mutex_(NULL),
        shmem_new_data_semaphore_(NULL),
        texture_cache_(TEXTURE_CACHE_SIZE, 0) {}

  virtual ~PreviewInstance() {
    texture_cache_.clear();
    CloseSharedMemory();
  }

  virtual bool Init(uint32_t argc, const char* argn[], const char* argv[]) {
    OpenSharedMemory();
    for (uint32_t i = 0; i < argc; i++) {
      std::string key = std::string(argn[i]);
      std::string value = std::string(argv[i]);
      if (key.compare("width")) {
        negotiated_width_ = atoi(value.c_str());
      } else if (key.compare("height")) {
        negotiated_height_ = atoi(value.c_str());
      }
    }
    return true;
  }

  virtual void DidChangeView(const pp::View& view) {
    // Pepper specifies dimensions in DIPs (device-independent pixels). To
    // generate a context that is at device-pixel resolution on HiDPI devices,
    // scale the dimensions by view.GetDeviceScale().

    int32_t new_width = view.GetRect().width() * view.GetDeviceScale();
    int32_t new_height = view.GetRect().height() * view.GetDeviceScale();

    if (negotiated_width_ != 0 && negotiated_height_ != 0) {
      new_width = negotiated_width_ * view.GetDeviceScale();
      new_height = negotiated_height_ * view.GetDeviceScale();
    }

    if (new_width == width_ && new_height == height_) {
      return;
    }

    if (context_.is_null()) {
      if (!InitGL(new_width, new_height)) {
        error("failed to init gl at resolution: %dx%d", new_width, new_height);
        return;
      }
      info("initialized context at resolution: %dx%d", new_width, new_height);

      InitShaders();
      InitBuffers();
      MainLoop(0);
    } else {
      // Resize the buffers to the new size of the module.
      int32_t result = context_.ResizeBuffers(new_width, new_height);
      if (result < 0) {
        error("failed to resize context buffers to %dx%d", new_width, new_height);
        return;
      }
      info("resized context to resolution: %dx%d", new_width, new_height);
    }

    width_ = new_width;
    height_ = new_height;
    glViewport(0, 0, width_, height_);
  }

  virtual void HandleMessage(const pp::Var& message) {
  }

 private:
  GLuint CompileShader(GLenum type, const char* data) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &data, NULL);
    glCompileShader(shader);

    GLint compile_status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
    if (compile_status != GL_TRUE) {
      // Shader failed to compile, let's see what the error is.
      char buffer[1024];
      GLsizei length;
      glGetShaderInfoLog(shader, sizeof(buffer), &length, &buffer[0]);
      error("failed to compile gl shader: %s", buffer);
      return 0;
    }

    return shader;
  }

  GLuint LinkProgram(GLuint frag_shader, GLuint vert_shader) {
    GLuint program = glCreateProgram();
    glAttachShader(program, frag_shader);
    glAttachShader(program, vert_shader);
    glLinkProgram(program);

    GLint link_status;
    glGetProgramiv(program, GL_LINK_STATUS, &link_status);
    if (link_status != GL_TRUE) {
      // Program failed to link, let's see what the error is.
      char buffer[1024];
      GLsizei length;
      glGetProgramInfoLog(program, sizeof(buffer), &length, &buffer[0]);
      error("failed to link gl program: %s", buffer);
      return 0;
    }
    return program;
  }


  bool InitGL(int32_t new_width, int32_t new_height) {
    if (!glInitializePPAPI(pp::Module::Get()->get_browser_interface())) {
      error("unable to initialize GL PPAPI!");
      return false;
    }

    const int32_t attrib_list[] = {
      PP_GRAPHICS3DATTRIB_ALPHA_SIZE, 8,
      PP_GRAPHICS3DATTRIB_DEPTH_SIZE, 24,
      PP_GRAPHICS3DATTRIB_WIDTH, new_width,
      PP_GRAPHICS3DATTRIB_HEIGHT, new_height,
      PP_GRAPHICS3DATTRIB_NONE
    };

    context_ = pp::Graphics3D(this, attrib_list);
    if (!BindGraphics(context_)) {
      error("failed to bind 3d graphics context!");
      context_ = pp::Graphics3D();
      glSetCurrentContextPPAPI(0);
      return false;
    }

    glSetCurrentContextPPAPI(context_.pp_resource());
    return true;
  }

  void InitShaders() {
    frag_shader_ = CompileShader(GL_FRAGMENT_SHADER, kFragShaderSource);
    if (!frag_shader_)
      return;

    vertex_shader_ = CompileShader(GL_VERTEX_SHADER, kVertexShaderSource);
    if (!vertex_shader_)
      return;

    program_ = LinkProgram(frag_shader_, vertex_shader_);
    if (!program_)
      return;

    texture_loc_ = glGetUniformLocation(program_, "u_texture");
    position_loc_ = glGetAttribLocation(program_, "a_position");
    texcoord_loc_ = glGetAttribLocation(program_, "a_texcoord");
    color_loc_ = glGetAttribLocation(program_, "a_color");
  }

  void InitBuffers() {
    glGenBuffers(1, &vertex_buffer_);
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kBoxVerts), &kBoxVerts[0],
                 GL_STATIC_DRAW);

    glGenBuffers(1, &index_buffer_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kBoxIndexes),
                 &kBoxIndexes[0], GL_STATIC_DRAW);
  }

  void CreateSharedTexture(GLint width, GLint height, GLuint64 handle, GLuint* texture, GLuint64* surface) {
    glGenTextures(1, texture);
    glCreatePbufferFromClientBufferEGL(width, height, handle, surface);
  }

  void BindSharedTexture(GLuint texture, GLuint64 surface) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindTexImageEGL(surface);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  void Render() {
    glClearColor(0.0, 0.0, 0.0, 1.0);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glUseProgram(program_);

    GLuint cur_texture = GetCurrentTexture();

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, cur_texture);
    glUniform1i(texture_loc_, 0);

    //define the attributes of the vertex
    glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
    glVertexAttribPointer(position_loc_,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, loc)));
    glEnableVertexAttribArray(position_loc_);
    glVertexAttribPointer(color_loc_,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, color)));
    glEnableVertexAttribArray(color_loc_);
    glVertexAttribPointer(texcoord_loc_,
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Vertex),
                          reinterpret_cast<void*>(offsetof(Vertex, tex)));
    glEnableVertexAttribArray(texcoord_loc_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_buffer_);

    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, 0);
  }

  void MainLoop(int32_t) {
    PushShmemFrameToQueue();
    Render();
    UnrefOldFrame();
    context_.SwapBuffers(
        callback_factory_.NewCallback(&PreviewInstance::MainLoop));
  }

  bool PushShmemFrameToQueue() {
    std::unique_ptr<PreviewFrame> preview_frame = NULL;
    GLuint        texture = 0;
    GLuint64      surface = 0;

    if (!GetAndWaitForShmemFrame(&preview_frame)) {
      return false;
    }

    if (preview_frame->shared_handle() == 0) {
      return false;
    }

    GLint width  = shmem_->video_info.width;
    GLint height = shmem_->video_info.height;
    std::string cache_key =
      std::to_string(width) + ":" +
      std::to_string(height) + ":" +
      std::to_string(preview_frame->shared_handle());
    if (texture_cache_.contains(cache_key)) {
      GLTextureFrame* texture_frame = texture_cache_.get(cache_key).get();
      texture = texture_frame->texture();
      surface = texture_frame->surface();
    } else {
      CreateSharedTexture(width, height, preview_frame->shared_handle(), &texture, &surface);
      BindSharedTexture(texture, surface);
      texture_cache_.insert(cache_key, std::make_shared<GLTextureFrame>(texture, surface));
    }

    preview_frame->SetTexture(texture);
    preview_frames_.emplace(std::move(preview_frame));
    return true;
  }

  GLuint GetCurrentTexture() {
    if (preview_frames_.empty()) {
      return 0;
    }
    return preview_frames_.back()->texture();
  }

  void CloseSharedMemory() {
    if (shmem_) {
      UnmapViewOfFile(shmem_);
      shmem_ = NULL;
    }

    if (shmem_handle_) {
      CloseHandle(shmem_handle_);
      shmem_handle_ = NULL;
    }

    if (shmem_mutex_) {
      CloseHandle(shmem_mutex_);
      shmem_mutex_ = NULL;
    }

    if (shmem_new_data_semaphore_) {
      CloseHandle(shmem_new_data_semaphore_);
      shmem_new_data_semaphore_ = NULL;
    }
  }

  bool OpenSharedMemory() {
    shmem_new_data_semaphore_ = OpenSemaphore(SYNCHRONIZE, false, BEBO_SHMEM_DATA_SEM);
    if (!shmem_new_data_semaphore_) {
      DWORD error = GetLastError();
      if (error == 2) {
        info("shared semaphore is not created yet, retrying in 500ms");
      } else {
        error("failed to open shared memory semaphore, %d", error);
      }
      Sleep(500);
      return false;
    }

    shmem_handle_ = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, false, BEBO_SHMEM_NAME);
    if (!shmem_handle_) {
      return false;
    }

    shmem_mutex_ = OpenMutexW(SYNCHRONIZE, false, BEBO_SHMEM_MUTEX);

    DWORD result = WaitForSingleObject(shmem_mutex_, INFINITE);
    if (result != WAIT_OBJECT_0) {
      return false;
    }

    shmem_ = (struct shmem*) MapViewOfFile(shmem_handle_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(struct shmem));
    if (!shmem_) {
      error("could not map shmem %d", GetLastError());
      ReleaseMutex(shmem_mutex_);
      return false;
    }

    uint64_t shmem_size = shmem_->shmem_size;
    uint64_t version = shmem_->version;

    UnmapViewOfFile(shmem_);
    shmem_ = nullptr;

    if (version != SHM_INTERFACE_VERSION) {
      ReleaseMutex(shmem_mutex_);
      error("SHM_INTERFACE_VERSION mismatch %d != %d", version, SHM_INTERFACE_VERSION);
      Sleep(3000);
      return false;
    }

    shmem_ = (struct shmem*) MapViewOfFile(shmem_handle_, FILE_MAP_ALL_ACCESS, 0, 0, shmem_size);
    shmem_->read_ptr = 0;
    video_width_ = shmem_->video_info.width;
    video_height_ = shmem_->video_info.height;

    ReleaseMutex(shmem_mutex_);

    if (!shmem_) {
      error("could not map shmem %d", GetLastError());
      Sleep(300);
      return false;
    }

    info("successfully opened shared memory buffer");
    return true;
  }

  bool GetAndWaitForShmemFrame(std::unique_ptr<PreviewFrame>* out_frame) {
    if (!shmem_ && !OpenSharedMemory()) {
      return false;
    }

    DWORD result = WaitForSingleObject(shmem_mutex_, INFINITE);
    if (result != WAIT_OBJECT_0) {
      return false;
    }

    DWORD wait_time_ms = 1000; // TODO: change it to indefinitely but need to support shutdown case
    while (shmem_->write_ptr == 0 || shmem_->read_ptr >= shmem_->write_ptr) {
      result = SignalObjectAndWait(shmem_mutex_,
          shmem_new_data_semaphore_,
          wait_time_ms,
          false);

      // re-attempt if we fail to acquire the semaphore
      if (result != WAIT_OBJECT_0) {
        ReleaseMutex(shmem_mutex_);
        return false;
      }

      // acquire the mutex to access shmem_ for later
      if (WaitForSingleObject(shmem_mutex_, INFINITE) != WAIT_OBJECT_0) {
        continue;
      }
    }

    if (shmem_->read_ptr == 0) {
      if (shmem_->write_ptr - shmem_->read_ptr > 0) {
        shmem_->read_ptr = shmem_->write_ptr;
        info("starting stream - resetting read pointer read_ptr: %d write_ptr: %d",
            shmem_->read_ptr, shmem_->write_ptr);
        UnrefBefore(shmem_->read_ptr);
      }
    } else if (shmem_->write_ptr - shmem_->read_ptr > shmem_->count) {
      uint64_t read_ptr = shmem_->write_ptr - shmem_->count / 2;
      info("late - resetting read pointer read_ptr: %d write_ptr: %d behind: %d new read_ptr: %d",
          shmem_->read_ptr, shmem_->write_ptr, shmem_->write_ptr - shmem_->read_ptr, read_ptr);
      shmem_->read_ptr = read_ptr;
      UnrefBefore(shmem_->read_ptr);
    }

    uint64_t i = shmem_->read_ptr % shmem_->count;
    uint64_t frame_offset = shmem_->frame_offset + i * shmem_->frame_size;
    struct frame *frame = (struct frame*) (((char*)shmem_) + frame_offset);
    frame->ref_cnt++;
    shmem_->read_ptr++;

    *out_frame = std::make_unique<PreviewFrame>(frame->nr, i, (uint64_t) frame->dxgi_handle);
    ReleaseMutex(shmem_mutex_);
    return true;
  }

  struct frame* GetShmFrame(uint64_t i) {
    uint64_t frame_offset = shmem_->frame_offset + i * shmem_->frame_size;
    return (struct frame*) (((char*)shmem_) + frame_offset);
  }

  void UnrefBefore(uint64_t before) {
    // expect to hold mutex!
    for (int i = 0; i < shmem_->count; i++) {
      auto shm_frame = GetShmFrame(i);
      if (shm_frame->ref_cnt < 2 && shm_frame->nr < before) {
        shm_frame->ref_cnt = 0;
      }
    }
  }

  void UnrefFrame(std::unique_ptr<PreviewFrame> frame) {
    if (WaitForSingleObject(shmem_mutex_, 1000) != WAIT_OBJECT_0) {
      return;
    }

    auto shm_frame = GetShmFrame(frame->ptr());
    if (shm_frame->nr == frame->nr()) {
      shm_frame->ref_cnt = shm_frame->ref_cnt - 2;
    }

    ReleaseMutex(shmem_mutex_);
  }

  void UnrefOldFrame() {
    if (preview_frames_.size() < WAIT_MIN_NUM_OF_FRAMES_TO_UNREF) {
      return;
    }
    std::unique_ptr<PreviewFrame> frame = std::move(preview_frames_.front());
    preview_frames_.pop();
    UnrefFrame(std::move(frame));
  }

  void PostTypedMessage(std::string type, pp::Var message) {
    pp::VarDictionary dictionary;
    dictionary.Set(pp::Var("type"), pp::Var(type));
    dictionary.Set(pp::Var("message"), message);
    PostMessage(dictionary);
  }

  void PostLogMessage(std::string severity, const char* format, ...) {
    // printf-like support
    char buffer[2048];
    va_list args;
    va_start(args, format);
    vsprintf_s(buffer, format, args);
    va_end(args);

    // enforce upper severity
    std::transform(severity.begin(), severity.end(), severity.begin(),
        [](char c){ return ::toupper(c); });

    pp::VarDictionary dictionary;
    dictionary.Set(pp::Var("type"), pp::Var("log"));
    dictionary.Set(pp::Var("severity"), pp::Var(severity));
    dictionary.Set(pp::Var("message"), pp::Var(buffer));
    PostMessage(dictionary);
  }

  pp::CompletionCallbackFactory<PreviewInstance> callback_factory_;
  pp::Graphics3D context_;

  int32_t width_;
  int32_t height_;
  int32_t negotiated_width_;
  int32_t negotiated_height_;
  GLuint frag_shader_;
  GLuint vertex_shader_;
  GLuint program_;
  GLuint vertex_buffer_;
  GLuint index_buffer_;

  std::queue<std::unique_ptr<PreviewFrame>> preview_frames_;
  lru::Cache<std::string, std::shared_ptr<GLTextureFrame>> texture_cache_;

  GLuint texture_loc_;
  GLuint position_loc_;
  GLuint texcoord_loc_;
  GLuint color_loc_;
  GLint video_width_;
  GLint video_height_;

  struct shmem* shmem_;
  HANDLE shmem_handle_;
  HANDLE shmem_mutex_;
  HANDLE shmem_new_data_semaphore_;
};

class Graphics3DModule : public pp::Module {
 public:
  Graphics3DModule() : pp::Module() {}
  virtual ~Graphics3DModule() {}

  virtual pp::Instance* CreateInstance(PP_Instance instance) {
    return new PreviewInstance(instance);
  }
};

namespace pp {
Module* CreateModule() { return new Graphics3DModule(); }
}  // namespace pp

