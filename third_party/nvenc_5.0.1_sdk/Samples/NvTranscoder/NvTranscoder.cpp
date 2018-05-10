#include <time.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include <stdio.h>
#include <string.h>
#include <cuda.h>

#include "VideoDecoder.h"
#include "VideoEncoder.h"
#include "../common/inc/nvUtils.h"

#ifdef _WIN32
DWORD WINAPI DecodeProc(LPVOID lpParameter)
{
    CudaDecoder* pDecoder = (CudaDecoder*)lpParameter;
    pDecoder->Start();

    return 0;
}

#else
void* DecodeProc(void *arg)
{
    CudaDecoder* pDecoder = (CudaDecoder*)arg;
    pDecoder->Start();

    return NULL;
}

#endif

void PrintHelp()
{
    printf("Usage : NvTranscoder \n"
        "-i <string>                  Specify input .h264 file\n"
        "-o <string>                  Specify output bitstream file\n"
        "\n### Optional parameters ###\n"
        "-size <int int>              Specify output resolution <width height>\n"
        "-codec <integer>             Specify the codec \n"
        "                                 0: H264\n"
        "                                 1: HEVC\n"
        "-preset <string>             Specify the preset for encoder settings\n"
        "                                 hq : nvenc HQ \n"
        "                                 hp : nvenc HP \n"
        "                                 lowLatencyHP : nvenc low latency HP \n"
        "                                 lowLatencyHQ : nvenc low latency HQ \n"
        "                                 lossless : nvenc Lossless HP \n"
        "-fps <integer>               Specify encoding frame rate\n"
        "-goplength <integer>         Specify gop length\n"
        "-numB <integer>              Specify number of B frames\n"
        "-bitrate <integer>           Specify the encoding average bitrate\n"
        "-vbvMaxBitrate <integer>     Specify the vbv max bitrate\n"
        "-vbvSize <integer>           Specify the encoding vbv/hrd buffer size\n"
        "-rcmode <integer>            Specify the rate control mode\n"
        "                                 0:  Constant QP\n"
        "                                 1:  Single pass VBR\n"
        "                                 2:  Single pass CBR\n"
        "                                 4:  Single pass VBR minQP\n"
        "                                 8:  Two pass frame quality\n"
        "                                 16: Two pass frame size cap\n"
        "                                 32: Two pass VBR\n"
        "-qp <integer>                Specify qp for Constant QP mode\n"
        "-deviceID <integer>          Specify the GPU device on which encoding will take place\n"
        "-help                        Prints Help Information\n\n"
        );
}

int main(int argc, char* argv[])
{
    __cu(cuInit(0));

    EncodeConfig encodeConfig = { 0 };
    encodeConfig.endFrameIdx = INT_MAX;
    encodeConfig.bitrate = 5000000;
    encodeConfig.rcMode = NV_ENC_PARAMS_RC_CONSTQP;
    encodeConfig.gopLength = NVENC_INFINITE_GOPLENGTH;
    encodeConfig.codec = NV_ENC_H264;
    encodeConfig.fps = 0;
    encodeConfig.qp = 28;
    encodeConfig.presetGUID = NV_ENC_PRESET_DEFAULT_GUID;
    encodeConfig.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;

    NVENCSTATUS nvStatus = CNvHWEncoder::ParseArguments(&encodeConfig, argc, argv);
    if (nvStatus != NV_ENC_SUCCESS)
    {
        PrintHelp();
        return 1;
    }

    if (!encodeConfig.inputFileName || !encodeConfig.outputFileName)
    {
        PrintHelp();
        return 1;
    }

    encodeConfig.fOutput = fopen(encodeConfig.outputFileName, "wb");
    if (encodeConfig.fOutput == NULL)
    {
        PRINTERR("Failed to create \"%s\"\n", encodeConfig.outputFileName);
        return 1;
    }

    //init cuda
    CUcontext cudaCtx;
    CUdevice device;
    __cu(cuDeviceGet(&device, encodeConfig.deviceID));
    __cu(cuCtxCreate(&cudaCtx, CU_CTX_SCHED_AUTO, device));

    CUcontext curCtx;
    CUvideoctxlock ctxLock;
    __cu(cuCtxPopCurrent(&curCtx));
    __cu(cuvidCtxLockCreate(&ctxLock, curCtx));

    CudaDecoder* pDecoder   = new CudaDecoder;
    FrameQueue* pFrameQueue = new CUVIDFrameQueue(ctxLock);
    pDecoder->InitVideoDecoder(encodeConfig.inputFileName, ctxLock, pFrameQueue, encodeConfig.width, encodeConfig.height);

    int decodedW, decodedH, decodedFRN, decodedFRD;
    pDecoder->GetCodecParam(&decodedW, &decodedH, &decodedFRN, &decodedFRD);

    if(encodeConfig.width <= 0 || encodeConfig.height <= 0) {
        encodeConfig.width  = decodedW;
        encodeConfig.height = decodedH;
    }

    if(encodeConfig.fps <= 0) {
        if (decodedFRN <= 0 || decodedFRD <= 0)
            encodeConfig.fps = 30;
        else
            encodeConfig.fps = decodedFRN / decodedFRD;
    }

    pFrameQueue->init(encodeConfig.width, encodeConfig.height);

    VideoEncoder* pEncoder = new VideoEncoder(ctxLock);
    assert(pEncoder->GetHWEncoder());

    nvStatus = pEncoder->GetHWEncoder()->Initialize(cudaCtx, NV_ENC_DEVICE_TYPE_CUDA);
    if (nvStatus != NV_ENC_SUCCESS)
        return 1;

    encodeConfig.presetGUID = pEncoder->GetHWEncoder()->GetPresetGUID(encodeConfig.encoderPreset, encodeConfig.codec);

    printf("Encoding input           : \"%s\"\n", encodeConfig.inputFileName);
    printf("         output          : \"%s\"\n", encodeConfig.outputFileName);
    printf("         codec           : \"%s\"\n", encodeConfig.codec == NV_ENC_HEVC ? "HEVC" : "H264");
    printf("         size            : %dx%d\n", encodeConfig.width, encodeConfig.height);
    printf("         bitrate         : %d bits/sec\n", encodeConfig.bitrate);
    printf("         vbvMaxBitrate   : %d bits/sec\n", encodeConfig.vbvMaxBitrate);
    printf("         vbvSize         : %d bits\n", encodeConfig.vbvSize);
    printf("         fps             : %d frames/sec\n", encodeConfig.fps);
    printf("         rcMode          : %s\n", encodeConfig.rcMode == NV_ENC_PARAMS_RC_CONSTQP ? "CONSTQP" :
        encodeConfig.rcMode == NV_ENC_PARAMS_RC_VBR ? "VBR" :
        encodeConfig.rcMode == NV_ENC_PARAMS_RC_CBR ? "CBR" :
        encodeConfig.rcMode == NV_ENC_PARAMS_RC_VBR_MINQP ? "VBR MINQP" :
        encodeConfig.rcMode == NV_ENC_PARAMS_RC_2_PASS_QUALITY ? "TWO_PASS_QUALITY" :
        encodeConfig.rcMode == NV_ENC_PARAMS_RC_2_PASS_FRAMESIZE_CAP ? "TWO_PASS_FRAMESIZE_CAP" :
        encodeConfig.rcMode == NV_ENC_PARAMS_RC_2_PASS_VBR ? "TWO_PASS_VBR" : "UNKNOWN");
    if (encodeConfig.gopLength == NVENC_INFINITE_GOPLENGTH)
        printf("         goplength       : INFINITE GOP \n");
    else
        printf("         goplength       : %d \n", encodeConfig.gopLength);
    printf("         B frames        : %d \n", encodeConfig.numB);
    printf("         QP              : %d \n", encodeConfig.qp);
    printf("         preset          : %s\n", (encodeConfig.presetGUID == NV_ENC_PRESET_LOW_LATENCY_HQ_GUID) ? "LOW_LATENCY_HQ" :
        (encodeConfig.presetGUID == NV_ENC_PRESET_LOW_LATENCY_HP_GUID) ? "LOW_LATENCY_HP" :
        (encodeConfig.presetGUID == NV_ENC_PRESET_HQ_GUID) ? "HQ_PRESET" :
        (encodeConfig.presetGUID == NV_ENC_PRESET_HP_GUID) ? "HP_PRESET" :
        (encodeConfig.presetGUID == NV_ENC_PRESET_LOSSLESS_HP_GUID) ? "LOSSLESS_HP" : "LOW_LATENCY_DEFAULT");
    printf("\n");

    nvStatus = pEncoder->GetHWEncoder()->CreateEncoder(&encodeConfig);
    if (nvStatus != NV_ENC_SUCCESS)
        return 1;

    nvStatus = pEncoder->AllocateIOBuffers(&encodeConfig);
    if (nvStatus != NV_ENC_SUCCESS)
        return 1;

    unsigned long long lStart, lEnd, lFreq;
    NvQueryPerformanceCounter(&lStart);

    //start decoding thread
#ifdef _WIN32
    HANDLE decodeThread = CreateThread(NULL, 0, DecodeProc, (LPVOID)pDecoder, 0, NULL);
#else
    pthread_t pid;
    pthread_create(&pid, NULL, DecodeProc, (void*)pDecoder);
#endif

    //start encoding thread
    while(!(pFrameQueue->isEndOfDecode() && pFrameQueue->isEmpty()) ) {

        CUVIDPARSERDISPINFO pInfo;
        if(pFrameQueue->dequeue(&pInfo)) {
            CUdeviceptr dMappedFrame = 0;
            unsigned int pitch;
            CUVIDPROCPARAMS oVPP = { 0 };
            oVPP.unpaired_field = 1;
            oVPP.progressive_frame = 1;

            cuvidMapVideoFrame(pDecoder->GetDecoder(), pInfo.picture_index, &dMappedFrame, &pitch, &oVPP);

            EncodeFrameConfig stEncodeConfig = { 0 };
            stEncodeConfig.dptr = dMappedFrame;
            stEncodeConfig.pitch = pitch;
            stEncodeConfig.width = encodeConfig.width;
            stEncodeConfig.height = encodeConfig.height;
            pEncoder->EncodeFrame(&stEncodeConfig);

            cuvidUnmapVideoFrame(pDecoder->GetDecoder(), dMappedFrame);
            pFrameQueue->releaseFrame(&pInfo);
       }
    }

    pEncoder->EncodeFrame(NULL, true);

#ifdef _WIN32
    WaitForSingleObject(decodeThread, INFINITE);
#else
    pthread_join(pid, NULL);
#endif

    if (pEncoder->GetEncodedFrames() > 0)
    {
        NvQueryPerformanceCounter(&lEnd);
        NvQueryPerformanceFrequency(&lFreq);
        double elapsedTime = (double)(lEnd - lStart)/(double)lFreq;
        printf("Total time: %fms, Decoded Frames: %d, Encoded Frames: %d, Average FPS: %f\n",
        elapsedTime * 1000,
        pDecoder->m_decodedFrames,
        pEncoder->GetEncodedFrames(),
        (float)pEncoder->GetEncodedFrames() / elapsedTime);
    }

    pEncoder->Deinitialize();
    delete pDecoder;
    delete pEncoder;
    delete pFrameQueue;

    cuvidCtxLockDestroy(ctxLock);
    __cu(cuCtxDestroy(cudaCtx));

    return 0;
}
