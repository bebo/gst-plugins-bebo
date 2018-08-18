// Copyright (c) 2013 The Chromium Authors. All rights reserved.  Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <GLES2/gl2.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "matrix.h"
#include "ppapi/cpp/graphics_3d.h"
#include "ppapi/cpp/instance.h"
#include "ppapi/cpp/module.h"
#include "ppapi/cpp/var.h"
#include "ppapi/cpp/var_array.h"
#include "ppapi/lib/gl/gles2/gl2ext_ppapi.h"
#include "ppapi/utility/completion_callback_factory.h"

#ifdef WIN32
#undef PostMessage
// Allow 'this' in initializer list
#pragma warning(disable : 4355)
#endif

extern const uint8_t kRLETextureData[];
extern const size_t kRLETextureDataLength;

namespace {

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
    fprintf(stderr, "Shader failed to compile: %s\n", buffer);
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
    fprintf(stderr, "Program failed to link: %s\n", buffer);
    return 0;
  }

  return program;
}

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

#define BEBO_SHMEM_NAME     L"BEBO_SHARED_MEMORY_BUFFER"
#define BEBO_SHMEM_MUTEX    L"BEBO_SHARED_MEMORY_BUFFER_MUTEX"
#define BEBO_SHMEM_DATA_SEM L"BEBO_SHARE_MEMORY_NEW_DATA_SEMAPHORE"
#include <windows.h>
struct shmem {
  uint64_t version;
  // GstVideoInfo video_info;
  uint64_t shmem_size;
  uint64_t frame_offset;
  uint64_t frame_size;
  uint64_t count;
  uint64_t write_ptr; // TODO better name - not really a ptr more like frame count
  uint64_t read_ptr;
};

void OpenSharedMemory() {
  OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, false, BEBO_SHMEM_NAME);
  OpenMutexW(SYNCHRONIZE, false, BEBO_SHMEM_MUTEX);
  // DebugBreak();
  // WaitForSingleObject(shmem_mutex_, INFINITE);

  // MapViewOfFile(shmem_handle_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(struct shmem));

  // UnmapViewOfFile(shmem_);
}

class Graphics3DInstance : public pp::Instance {
 public:
  explicit Graphics3DInstance(PP_Instance instance)
      : pp::Instance(instance),
        callback_factory_(this),
        width_(0),
        height_(0),
        frag_shader_(0),
        vertex_shader_(0),
        program_(0),
        texture_loc_(0),
        position_loc_(0),
        color_loc_(0) {}

  virtual bool Init(uint32_t argc, const char* argn[], const char* argv[]) {
    OpenSharedMemory();
    return true;
  }

  virtual void DidChangeView(const pp::View& view) {
    // Pepper specifies dimensions in DIPs (device-independent pixels). To
    // generate a context that is at device-pixel resolution on HiDPI devices,
    // scale the dimensions by view.GetDeviceScale().
    int32_t new_width = view.GetRect().width() * view.GetDeviceScale();
    int32_t new_height = view.GetRect().height() * view.GetDeviceScale();

    if (context_.is_null()) {
      if (!InitGL(new_width, new_height)) {
        // failed.
        return;
      }

      InitShaders();
      InitBuffers();
      InitTexture();
      MainLoop(0);
    } else {
      // Resize the buffers to the new size of the module.
      int32_t result = context_.ResizeBuffers(new_width, new_height);
      if (result < 0) {
        fprintf(stderr,
                "Unable to resize buffers to %d x %d!\n",
                new_width,
                new_height);
        return;
      }
    }

    width_ = new_width;
    height_ = new_height;
    glViewport(0, 0, width_, height_);
  }

  virtual void HandleMessage(const pp::Var& message) {
    // An array message sets the current x and y rotation.
    if (!message.is_array()) {
      fprintf(stderr, "Expected array message.\n");
      return;
    }

    pp::VarArray array(message);
    if (array.GetLength() != 2) {
      fprintf(stderr, "Expected array of length 2.\n");
      return;
    }
  }

 private:
  bool InitGL(int32_t new_width, int32_t new_height) {
    if (!glInitializePPAPI(pp::Module::Get()->get_browser_interface())) {
      fprintf(stderr, "Unable to initialize GL PPAPI!\n");
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
      fprintf(stderr, "Unable to bind 3d context!\n");
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

  void InitTexture() {
    glGenAndBindSharedHandleTexture(1, 1280, 720, 2147488002, &texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  void Render() {
    glClearColor(0.0, 0.0, 0.0, 1);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);

    // TODO: rotate through available textures
    glBindTexture(GL_TEXTURE_2D, texture_);

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
    Render();
    context_.SwapBuffers(
        callback_factory_.NewCallback(&Graphics3DInstance::MainLoop));
  }

  pp::CompletionCallbackFactory<Graphics3DInstance> callback_factory_;
  pp::Graphics3D context_;
  int32_t width_;
  int32_t height_;
  GLuint frag_shader_;
  GLuint vertex_shader_;
  GLuint program_;
  GLuint vertex_buffer_;
  GLuint index_buffer_;
  GLuint texture_;

  GLuint texture_loc_;
  GLuint position_loc_;
  GLuint texcoord_loc_;
  GLuint color_loc_;

  /*
  HANDLE shmem_handle_;
  struct shmem *shmem_;
  HANDLE shmem_mutex_;
  HANDLE shmem_new_data_semaphore_;
  */
};

class Graphics3DModule : public pp::Module {
 public:
  Graphics3DModule() : pp::Module() {}
  virtual ~Graphics3DModule() {}

  virtual pp::Instance* CreateInstance(PP_Instance instance) {
    return new Graphics3DInstance(instance);
  }
};

namespace pp {
Module* CreateModule() { return new Graphics3DModule(); }
}  // namespace pp
