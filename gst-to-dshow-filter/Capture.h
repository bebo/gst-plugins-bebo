//------------------------------------------------------------------------------
// File: capture.h
//
// Desc: DirectShow sample code - In-memory push mode source filter
//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//------------------------------------------------------------------------------
#ifndef CAPTURE_H
#define CAPTURE_H

#include <queue>
#include <windows.h>
#include <streams.h>
#include <strsafe.h>
#include <d3d11.h>
#include <d3d11_3.h>
#include <dxgi.h>

#include "../shared/bebo_shmem.h"
#include "registry.h"
#include "names_and_ids.h"
#include <wrl.h>

using Microsoft::WRL::ComPtr;

class CPushPinDesktop;
class DxgiFrame;


// parent
class CGameCapture : public CSource // public IAMFilterMiscFlags // CSource is CBaseFilter is IBaseFilter is IMediaFilter is IPersist which is IUnknown
{

  private:
    // Constructor is private because you have to use CreateInstance
    CGameCapture(IUnknown *pUnk, HRESULT *phr);
    ~CGameCapture();

    CPushPinDesktop *m_pPin;
  public:
    //////////////////////////////////////////////////////////////////////////
    //  IUnknown
    //////////////////////////////////////////////////////////////////////////
    static CUnknown * WINAPI CreateInstance(LPUNKNOWN lpunk, HRESULT *phr);
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv);

    // ?? compiler error that these be required here? huh?
    ULONG STDMETHODCALLTYPE AddRef() { return CBaseFilter::AddRef(); };
    ULONG STDMETHODCALLTYPE Release() { return CBaseFilter::Release(); };

    ////// 
    // IAMFilterMiscFlags, in case it helps somebody somewhere know we're a source config (probably unnecessary)
    //////
    // ULONG STDMETHODCALLTYPE GetMiscFlags() { return AM_FILTER_MISC_FLAGS_IS_SOURCE; } 
    // not sure if we should define the above without also implementing  IAMPushSource interface.

    // our own method
    IFilterGraph *GetGraph() {return m_pGraph;}

    // CBaseFilter, some pdf told me I should (msdn agrees)
    STDMETHODIMP GetState(DWORD dwMilliSecsTimeout, FILTER_STATE *State);
    STDMETHODIMP Stop(); //http://social.msdn.microsoft.com/Forums/en/windowsdirectshowdevelopment/thread/a9e62057-f23b-4ce7-874a-6dd7abc7dbf7
};

enum TimeOffsetType
{
  TIME_OFFSET_NONE,
  TIME_OFFSET_DTS,
  TIME_OFFSET_PTS
};

// child
class CPushPinDesktop : public CSourceStream, public IAMStreamConfig, public IKsPropertySet //CSourceStream is ... CBasePin
{

  protected:
    RegKey registry;

    REFERENCE_TIME m_rtFrameLength; // also used to get the fps

    int getNegotiatedFinalWidth();
    int getNegotiatedFinalHeight();

    HANDLE shmem_handle_;
    struct shmem *shmem_ = nullptr;
    HANDLE shmem_mutex_;
    HANDLE shmem_new_data_semaphore_;

    int m_iCaptureConfigWidth = 0;
    int m_iCaptureConfigHeight = 0;

    CGameCapture* m_pParent;

    bool m_bFormatAlreadySet = false;
    volatile bool active = false;

    REFERENCE_TIME last_frame_ = 0;
    int64_t time_offset_dts_ns_ = 0;
    int64_t time_offset_pts_ns_ = 0;
    TimeOffsetType time_offset_type_ = TIME_OFFSET_NONE;

    uint64_t frame_start_ = 0;
    uint64_t frame_total_cnt_ = 0;
    uint64_t frame_sent_cnt_ = 0;
    uint64_t frame_dropped_cnt_ = 0;
    uint64_t frame_late_cnt_ = 0;
    uint64_t first_frame_ms_ = 0;
    long double frame_processing_time_ms_ = 0.0;

    ComPtr<ID3D11Device> d3d_device_;
    ComPtr<ID3D11DeviceContext> d3d_context_;

    wchar_t out_[1024];

    virtual HRESULT DoBufferProcessingLoop(void) override;
    virtual HRESULT InitAllocator(IMemAllocator** ppAllocator) override;
    virtual HRESULT DecideAllocator(IMemInputPin* pPin, IMemAllocator** ppAlloc) override;

    float GetFps();
    int getCaptureDesiredFinalWidth();
    int getCaptureDesiredFinalHeight();


    // STDMETHODIMP SuggestAllocatorProperties(const ALLOCATOR_PROPERTIES* pprop) override;
    // STDMETHODIMP GetAllocatorProperties(ALLOCATOR_PROPERTIES* pprop) override;

  public:
    //CSourceStream overrrides
    HRESULT Inactive(void);
    HRESULT Active(void);


    //////////////////////////////////////////////////////////////////////////
    //  IUnknown
    //////////////////////////////////////////////////////////////////////////
    STDMETHODIMP QueryInterface(REFIID riid, void **ppv); 
    STDMETHODIMP_(ULONG) AddRef() { return GetOwner()->AddRef(); } // gets called often...
    STDMETHODIMP_(ULONG) Release() { return GetOwner()->Release(); }


    //////////////////////////////////////////////////////////////////////////
    //  IAMStreamConfig
    //////////////////////////////////////////////////////////////////////////
    HRESULT STDMETHODCALLTYPE SetFormat(AM_MEDIA_TYPE *pmt);
    HRESULT STDMETHODCALLTYPE GetFormat(AM_MEDIA_TYPE **ppmt);
    HRESULT STDMETHODCALLTYPE GetNumberOfCapabilities(int *piCount, int *piSize);
    HRESULT STDMETHODCALLTYPE GetStreamCaps(int iIndex, AM_MEDIA_TYPE **pmt, BYTE *pSCC);

    CPushPinDesktop(HRESULT *phr, CGameCapture *pFilter);
    ~CPushPinDesktop();

    HRESULT OpenShmMem();

    // Override the version that offers exactly one media type
    HRESULT DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pRequest);
    HRESULT FillBuffer(IMediaSample *pSample);
    HRESULT FillBuffer(IMediaSample *pSample, DxgiFrame** out_dxgi_frame);
    HRESULT FillBufferFromShMem(DxgiFrame *dxgi_frame, REFERENCE_TIME *startFrame, REFERENCE_TIME *endFrame, bool *discontinuity, DWORD wait_time_ms);
    struct frame * GetShmFrame(uint64_t index);
    struct frame * GetShmFrame(DxgiFrame* dxgi_frame);
    HRESULT UnrefDxgiFrame(DxgiFrame* dxgi_frame);
    HRESULT UnrefBefore(uint64_t i);

    // Set the agreed media type and set up the necessary parameters
    HRESULT SetMediaType(const CMediaType *pMediaType);

    // Support multiple display formats (CBasePin)
    HRESULT CheckMediaType(const CMediaType *pMediaType);
    HRESULT GetMediaType(int iPosition, CMediaType *pmt);


    // IQualityControl
    // Not implemented because we aren't going in real time.
    // If the file-writing filter slows the graph down, we just do nothing, which means
    // wait until we're unblocked. No frames are ever dropped.
    STDMETHODIMP Notify(IBaseFilter *pSelf, Quality q)
    {
      return E_FAIL;
    }

    //////////////////////////////////////////////////////////////////////////
    //  IKsPropertySet
    //////////////////////////////////////////////////////////////////////////
    HRESULT STDMETHODCALLTYPE Set(REFGUID guidPropSet, DWORD dwID, void *pInstanceData, DWORD cbInstanceData, void *pPropData, DWORD cbPropData);
    HRESULT STDMETHODCALLTYPE Get(REFGUID guidPropSet, DWORD dwPropID, void *pInstanceData,DWORD cbInstanceData, void *pPropData, DWORD cbPropData, DWORD *pcbReturned);
    HRESULT STDMETHODCALLTYPE QuerySupported(REFGUID guidPropSet, DWORD dwPropID, DWORD *pTypeSupport);

  private:
    std::queue<DxgiFrame*> dxgi_frame_queue_;

    HRESULT CreateDeviceD3D11(IDXGIAdapter *adapter);
    HRESULT InitializeDXGI();

    D3D11_TEXTURE2D_DESC ConvertToStagingTexture(D3D11_TEXTURE2D_DESC share_desc);

    HRESULT CopyTextureToStaging(DxgiFrame* frame);
    HRESULT PushFrameToMediaSample(DxgiFrame* frame, IMediaSample* media_sample);
    DWORD GetReadyFrameFromQueue(DxgiFrame** out_frame);

    // QueueFrameFromShm
    // WrapAndCopy
    // IsHeadTextureReady - if frame.head() is ready? check time
      // WaitUntilSomethingReady (shm, timeout), if timeout -> map, if new frame -> schedule
    // CopyToCpu
    // Ship it!
};

class DxgiFrame {
  public:
    HANDLE dxgi_handle;
    uint64_t nr;
    uint64_t index;
    bool texture_mapped_to_memory;
    REFERENCE_TIME texture_timestamp;
    ComPtr<ID3D11Texture2D> texture;


    DxgiFrame();
    ~DxgiFrame();

    void SetFrame(struct frame *frameData, uint64_t i);
};

class DxgiFramePool {
};

#endif
