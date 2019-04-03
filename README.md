## gst-plugins-bebo
This is a collection of gstreamer plugins that we wrote for our livestreaming application.


## Building
* CMake 3.8+ is required.
* Windows SDK 15063 (any version, but need to update CMakeLists). Right now the version is hardcoded. It can be switched to a CMake function to look for the SDK.

```
For CMake 3.14:
cmake -G "Visual Studio 15 2017 Win64" -S . -B build
cmake --build build

Older CMake:
mkdir build
cd build
cmake -G "Visual Studio 15 2017 Win64" ..
cd ..
cmake --build build
```


## Notes
* "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.15063.0/um/x64"


## License
The source code provied by Pigs in Flight Inc. is licensed under the MIT
license.

Different parts of the project are under different licenses depending on their
respective origin.

For the full text of the licenses see: LICENSE.TXT
