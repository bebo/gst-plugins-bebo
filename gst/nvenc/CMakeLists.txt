
CMAKE_MINIMUM_REQUIRED (VERSION 2.8)

PROJECT(libgstd3dnvenc)

SET(CUDA_PATH "C:\\Program Files\\NVIDIA GPU Computing Toolkit\\CUDA\\v9.1\\include")

SET_PROPERTY(
  DIRECTORY
  APPEND PROPERTY COMPILE_DEFINITIONS
  HAVE_CONFIG_H
)

INCLUDE_DIRECTORIES(
  ${GST_INSTALL_BASE}/include
  ${GST_INSTALL_BASE}/include/gstreamer-1.0
  ${GST_INSTALL_BASE}/include/glib-2.0
  ${GST_INSTALL_BASE}/lib/glib-2.0/include
  ${GST_INSTALL_BASE}/lib/gstreamer-1.0/include
  ${PROJECT_SOURCE_DIR}/../../win32/common
  ${NVENC_SDK_BASE}/Samples/NvCodec/NvEncoder
  ${CMAKE_SOURCE_DIR}/gst-libs/gst
  ${CUDA_PATH}
)

LINK_DIRECTORIES(
  ${GST_INSTALL_BASE}/lib
  "C:\\Program Files\\NVIDIA GPU Computing Toolkit\\CUDA\\v9.1\\include"
  "C:\\Program Files\\NVIDIA GPU Computing Toolkit\\CUDA\\v9.1\\lib\\x64"
)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT /Zi")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")

ADD_LIBRARY(libgstd3dnvenc SHARED
  ${CMAKE_SOURCE_DIR}/gst-libs/gst/dxgi/gstdxgimemory.c
  ${CMAKE_SOURCE_DIR}/gst-libs/gst/dxgi/gstdxgimemory.h
  config.h
  gstnvbaseenc.c
  gstnvbaseenc.h
  gstnvenc.c
  gstnvenc.h
  gstnvh264enc.c
  gstnvh264enc.h
  ${CMAKE_SOURCE_DIR}/gst-libs/gst/dxgi/gstdxgimemory.c
  ${CMAKE_SOURCE_DIR}/gst-libs/gst/dxgi/gstdxgimemory.h
)

TARGET_LINK_LIBRARIES(libgstd3dnvenc
  gstreamer-1.0
  gstaudio-1.0
  gstvideo-1.0
  gstbase-1.0
  gstgl-1.0
  glib-2.0
  gobject-2.0
  rpcrt4
  uuid
  winmm
  cuda
  cudart
  opengl32
  D3d11
)
