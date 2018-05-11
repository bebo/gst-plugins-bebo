cmake_minimum_required(VERSION 3.8)

set(CMAKE_CONFIGURATION_TYPES Release Debug)

add_definitions(-DGST_USE_UNSTABLE_API)

project(bebo-gst)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(GST_INSTALL_BASE "${CMAKE_SOURCE_DIR}/third_party/gstreamer" CACHE INTERNAL "Path to the GStreamer install base")
set(NVENC_SDK_BASE "${CMAKE_SOURCE_DIR}/third_party/Video_Codec_SDK_8.1.24" CACHE INTERNAL "Path to NVENC SDK")


# Use folders in the resulting project files.
set_property(GLOBAL PROPERTY OS_FOLDERS ON)

#
# Fetch dependencies.
#
find_package(D3D11 REQUIRED)
include(DownloadDependency)
DownloadDependency("https://s3-us-west-1.amazonaws.com/bebo-app/repo/gstreamer-base/gstreamer.tar.bz2" "gstreamer")

#
# Target configuration.
#
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/gst/nvenc)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/gst/dshowfiltersink)
