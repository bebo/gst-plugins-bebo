



## Build Dependencies

* Visual Studio 2015 (C++)
* https://developer.nvidia.com/cuda-downloads

# gstreamer to directshow
```
   +------------------------------+       +-------------------------------+
   |                              |       |                               |
   |          gstreamer           |       |          DirectShow           |
   |                              |       |                               |
   |      +-------------------+   |       |   +--------------------+      |
   |      |                   |   |       |   |                    |      |
   |   --->  dshowfiltersink  +--------------->    gst-to-dshow    +-->   |
   |      |    (sink plugin)  |   |       |   |  (capture filter)  |      |
   |      |                   |   |       |   |                    |      |
   |      +-------------------+   |       |   +--------------------+      |
   |                              |       |                               |
   +------------------------------+       +-------------------------------+
```

This projects provides gstreamer to Direct Show Capture Filter bridge

Currently tested on Windows 10 64 bit

# Build


## To register the DLL as a Direct Show Capture Service

run cmd as __Administrator__:
```
regsvr32 gst-to-dshow.dll
```

## To test that gst produces some frames

### Produce some frames from gst
```
set GST_PLUGIN_PATH=.
gst-launch-1.0 videotestsrc ! "video/x-raw,framerate=30/1,height=720,width=1280" ! videoconvert ! "video/x-raw,format=I420" !  glupload ! gldownload ! dshowfiltersink
```

### Show DirectShow sees some frames
```
ffplay -f dshow -video_size 1280x720 -i video=bebo-gst-to-dshow
```

## libyuv (notes)


You may want to build your own copy of libyuv 
* last libyuv verion used: 54289f1bb0c78afdab73839c67989527f3237912
* depot tools (for libyuv) https://www.chromium.org/developers/how-tos/install-depot-tools
* also see https://chromium.googlesource.com/libyuv/libyuv/+/master/docs/getting_started.md

```
SET DEPOT_TOOLS_WIN_TOOLCHAIN=0
set GYP_DEFINES=clang=1 target_arch=x64
gclient config --name src https://chromium.googlesource.com/libyuv/libyuv
gclient sync
cd src
call python tools\clang\scripts\update.py
call gn gen out/Release "--args=is_debug=false is_official_build=true is_clang=true target_cpu=\"x64\""
ninja -v -C out/Release
```
find libyuv_internal.lib and the include directory


# Attributions / History
* We started this code based on the Direct Show Desktop Capture Filter:
  https://github.com/rdp/screen-capture-recorder-to-video-windows-free
* All direct show filters make heavy use of the Micosoft DirectShow SDK
  BaseClasses
* We use the super fast g2log by Kjell Hedstroem for logging
* We use several of the chromium base classes / infrastructure

# License

The source code provied by Pigs in Flight Inc. is licensed under the MIT
license.

Different parts of the project are under different licenses depending on their
respective origin.

For the full text of the licenses see: LICENSE.TXT
