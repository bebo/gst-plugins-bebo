cmake_minimum_required(VERSION 3.8)

set(CMAKE_CONFIGURATION_TYPES Release Debug)

add_definitions(-DGST_USE_UNSTABLE_API)

project(bebo-gst)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(GST_INSTALL_BASE "${CMAKE_SOURCE_DIR}/third_party/gstreamer" CACHE INTERNAL "Path to the GStreamer install base")
set(NVENC_SDK_BASE "${CMAKE_SOURCE_DIR}/third_party/Video_Codec_SDK_7.1.9" CACHE INTERNAL "Path to NVENC SDK")


# Use folders in the resulting project files.
set_property(GLOBAL PROPERTY OS_FOLDERS ON)

#
# Fetch dependencies.
#
find_package(D3D11 REQUIRED)
include(DownloadDependency)
include(DownloadZip)
DownloadDependency("https://s3-us-west-1.amazonaws.com/bebo-app/repo/gstreamer-base/gstreamer_meson_1.15.0.1pre0.zip" "gstreamer")
DownloadDependency("https://s3-us-west-1.amazonaws.com/bebo-app/repo/third_party/Video_Codec_SDK_7.1.9.tar.bz2" "Video_Codec_SDK_7.1.9")
DownloadDependency("https://s3-us-west-1.amazonaws.com/bebo-app/repo/libspeexdsp/libspeexdsp-1-2rc3.tar.bz2" "speexdsp")
DownloadZip("https://s3-us-west-1.amazonaws.com/bebo-app/repo/nacl-sdk/nacl-sdk_pepper_56-20180827140914.zip" "naclsdk")


#
# Target configuration.
#
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/gst)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/nacl-preview)
