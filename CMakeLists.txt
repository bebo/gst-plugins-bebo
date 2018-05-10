cmake_minimum_required(VERSION 3.8)

set(CMAKE_CONFIGURATION_TYPES Release Debug)

project(bebo-gst)


SET(GST_INSTALL_BASE "D:\\gstreamer\\1.0\\x86_64" CACHE PATH "Path to the GStreamer install base")
SET(NVENC_SDK_BASE "${PROJECT_SOURCE_DIR}\\third_party\\nvenc_5.0.1_sdk" CACHE PATH "Path to NVENC SDK")


# Use folders in the resulting project files.
set_property(GLOBAL PROPERTY OS_FOLDERS ON)


#
# Target configuration.
#
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/gst/nvenc)
