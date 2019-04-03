## nacl-preview

This is a chrome's NaCL Plugin (https://developer.chrome.com/native-client) that render textures by using their shared handle. In the current implementation, this plugin gets the shared texture handle from shared memory region.


There's a companion gstreamer sink plugin that write video frames textures' shared handle into a shared memory region, then the nacl plugin renders them.


**Notice**
The current implementation is optimized for performance, which requires a patch on chromium to expose EGL interfaces. [Reference to the patch](https://github.com/bebo/chromium.src/commit/6343b5477f37c7c077f58f9c0421e9070bf05e9d)


If you do not want to patch chromium, you can still make this plugin useful by replacing the use of EGL functions to GL. You may have to interop / copy the textures.

