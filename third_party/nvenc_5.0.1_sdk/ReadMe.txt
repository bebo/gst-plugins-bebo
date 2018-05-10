NVENC 5.0 SDK Readme and Getting Started Guide

System Requirements

* NVIDIA Kepler/Maxwell based GPU - Refer to the NVIDIA NVENC developer zone web page (https://developer.nvidia.com/nvidia-video-codec-sdk) for GPUs that support NVENC
* Windows: Driver version 347.09 or higher
* Linux: Driver 346.22 or higher
* CUDA 6.5 Toolkit

[Windows Configuration Requirements]
The following environment variables need to be set to build the sample applications included with the SDK
* For Windows
  - DXSDK_DIR: pointing to the DirectX SDK root directory
    You can download the latest SDK from Microsoft's DirectX website
* For Windows
  - The CUDA 6.5 Toolkit must be installed (see below on how to get it)
  - CUDA toolkit required to enable CUDA interoperability support with NVENC hardware

[Linux Configuration Requirements]    
* For Linux
  - The CUDA 6.5 Toolkit must be installed (see below on how to get it)

[Common to all OS platforms]
To download the CUDA 6.5 toolkit, please go to the following web site:
http://developer.nvidia.com/cuda/cuda-toolkit

Please refer to the samples guide [<SDK Installation Folder>\Samples\NVENC_Samples_Guide.pdf] for details regarding the building and running of the sample applications included with the SDK. 