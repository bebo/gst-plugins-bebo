PROJECT(naclpreview)

add_definitions(-DUNICODE)

SET_PROPERTY(
  DIRECTORY
  APPEND PROPERTY COMPILE_DEFINITIONS
)

INCLUDE_DIRECTORIES(
  ${CMAKE_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/../../win32/common
  ${CMAKE_SOURCE_DIR}/gst-libs/gst/dxgi/
  ${CMAKE_SOURCE_DIR}/shared
  ${CMAKE_SOURCE_DIR}/third_party/naclsdk/include
  ${GST_INSTALL_BASE}/include
  ${GST_INSTALL_BASE}/include/gstreamer-1.0
  ${GST_INSTALL_BASE}/include/glib-2.0
  ${GST_INSTALL_BASE}/lib/glib-2.0/include
)

LINK_DIRECTORIES(
  "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.15063.0/um/x64"
  ${CMAKE_SOURCE_DIR}/third_party/naclsdk/lib/Release # ${CMAKE_BUILD_TYPE}
)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT /Zi")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /DEBUG /OPT:REF /OPT:ICF")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /MTd")

SET(shared_FILES
  ${CMAKE_SOURCE_DIR}/shared/bebo_shmem.h
  ${CMAKE_SOURCE_DIR}/shared/config.h
)

SET(preview_FILES
  preview.cc
  lru_cache.h
)

source_group("shared" FILES ${shared_FILES})
source_group("preview" FILES ${preview_FILES})

ADD_LIBRARY(naclpreview SHARED
  ${shared_FILES}
  ${preview_FILES}
)

TARGET_LINK_LIBRARIES(naclpreview
  # pappi
  ppapi
  ppapi_cpp
  ppapi_gles2
  # windows
  d3d11
  kernel32
)

