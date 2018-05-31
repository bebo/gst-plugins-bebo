
PROJECT(libbebogst)

SET_PROPERTY(
  DIRECTORY
  APPEND PROPERTY COMPILE_DEFINITIONS
  HAVE_CONFIG_H
)

INCLUDE_DIRECTORIES(
  ${CMAKE_SOURCE_DIR}
  ${GST_INSTALL_BASE}/include
  ${GST_INSTALL_BASE}/include/gstreamer-1.0
  ${GST_INSTALL_BASE}/include/glib-2.0
  ${GST_INSTALL_BASE}/lib/glib-2.0/include
  ${GST_INSTALL_BASE}/lib/gstreamer-1.0/include
  ${PROJECT_SOURCE_DIR}/../../win32/common
  ${CMAKE_SOURCE_DIR}/gst-libs/gst/dxgi/
)

LINK_DIRECTORIES(
  ${GST_INSTALL_BASE}/lib
  "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.15063.0/um/x64"
)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT /Zi")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")

SET(shared_SOURCES
  ${CMAKE_SOURCE_DIR}/gst-libs/gst/dxgi/gstdxgimemory.c
  ${CMAKE_SOURCE_DIR}/gst-libs/gst/dxgi/gstdxgidevice.c
)

SET(shared_HEADERS
  ${CMAKE_SOURCE_DIR}/shared/bebo_shmem.h
  ${CMAKE_SOURCE_DIR}/shared/config.h
  ${CMAKE_SOURCE_DIR}/gst-libs/gst/dxgi/gstdxgidevice.h
  ${CMAKE_SOURCE_DIR}/gst-libs/gst/dxgi/gstdxgimemory.h
)

SET(nvenc_SOURCES
  nvenc/gstnvbaseenc.c
  nvenc/gstnvenc.c
  nvenc/gstnvh264enc.c
)

SET(nvenc_HEADERS
  nvenc/gstnvbaseenc.h
  nvenc/gstnvenc.h
  nvenc/gstnvh264enc.h
)

SET(gstdshowsink_SOURCES
  dshowfiltersink/gstdshowsink.c
)

SET(gstdshowsink_HEADERS
  dshowfiltersink/gstdshowsink.h
)

SET(gl2dxgi_SOURCES
  gl2dxgi/gstgl2dxgi.c
)

SET(gl2dxgi_HEADERS
  gl2dxgi/gstgl2dxgi.h
)

SET(bebosquisher_SOURCES
  bebosquisher/gstglbasemixer.c
  bebosquisher/gstglmixerbin.c
  bebosquisher/gstglmixer.c
  bebosquisher/gstglvideomixer.c
  bebosquisher/gstglstereomix.c
  bebosquisher/gstglutils.c
)

SET(bebosquisher_HEADERS
  bebosquisher/gstglbasemixer.h
  bebosquisher/gstglmixerbin.h
  bebosquisher/gstglmixer.h
  bebosquisher/gstglvideomixer.h
  bebosquisher/gstglstereomix.h
  bebosquisher/gstglutils.h
)

source_group("nvenc" FILES ${nvenc_SOURCES} ${nvenc_HEADERS})
source_group("gstdshowsink" FILES ${gstdshowsink_SOURCES} ${gstdshowsink_HEADERS})
source_group("gl2dxgi" FILES ${gl2dxgi_SOURCES} ${gl2dxgi_HEADERS})
source_group("shared" FILES ${shared_SOURCES} ${shared_HEADERS})
source_group("plugin_init" FILES gstbeboplugin.c)
source_group("bebosquisher" FILES ${bebosquisher_SOURCES} ${bebosquisher_HEADERS})


ADD_LIBRARY(libbebogst SHARED
  gstbeboplugin.c
  ${shared_SOURCES}
  ${shared_HEADERS}
  ${nvenc_SOURCES}
  ${nvenc_HEADERS}
  ${gstdshowsink_SOURCES}
  ${gstdshowsink_HEADERS}
  ${gl2dxgi_SOURCES}
  ${gl2dxgi_HEADERS}
  ${bebosquisher_SOURCES}
  ${bebosquisher_HEADERS}
)

TARGET_LINK_LIBRARIES(libbebogst
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
  D3D11
  opengl32
  gstallocators-1.0
  gstbadallocators-1.0
  gstbadbase-1.0
  gstbadvideo-1.0
  gstpbutils-1.0
  gstcontroller-1.0
)

INCLUDE_DIRECTORIES(
  ${GST_INSTALL_BASE}/include
  ${GST_INSTALL_BASE}/include/gstreamer-1.0
  ${GST_INSTALL_BASE}/include/glib-2.0
  ${GST_INSTALL_BASE}/lib/glib-2.0/include
  ${GST_INSTALL_BASE}/lib/gstreamer-1.0/include
  ${PROJECT_SOURCE_DIR}/../../win32/common
  ${NVENC_SDK_BASE}/Samples/NvCodec/NvEncoder
  ${NVENC_SDK_BASE}/Samples/Common/inc
  ${CMAKE_SOURCE_DIR}/gst-libs/gst
  ${CMAKE_SOURCE_DIR}/gst
  ${CMAKE_SOURCE_DIR}/shared
  ${CUDA_PATH}
)

LINK_DIRECTORIES(
  ${GST_INSTALL_BASE}/lib
)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT /Zi")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
