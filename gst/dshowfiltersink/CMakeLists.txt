
PROJECT(libgstdshowfiltersink)

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

ADD_LIBRARY(libgstdshowfiltersink SHARED
  config.h
  gstdshowfilter.c
  gstdshowsink.c
  gstdshowsink.h
  ${CMAKE_SOURCE_DIR}/gst-libs/gst/dxgi/gstdxgimemory.c
  ${CMAKE_SOURCE_DIR}/gst-libs/gst/dxgi/gstdxgimemory.h
  ${CMAKE_SOURCE_DIR}/shared/bebo_shmem.h
)

TARGET_LINK_LIBRARIES(libgstdshowfiltersink
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
)
