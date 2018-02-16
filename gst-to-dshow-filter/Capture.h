//------------------------------------------------------------------------------
// File: capture.h
//
// Desc: DirectShow sample code - In-memory push mode source filter
//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//------------------------------------------------------------------------------
#ifndef CAPTURE_H
#define CAPTURE_H

#include <strsafe.h>
#include "CommonTypes.h"
#include "registry.h"
#include "../shared/bebo_shmem.h"
#include "names_and_ids.h"

class CPushPinDesktop;

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

    REFERENCE_TIME m_rtFrameLength = UNITS / 30 ; // also used to get the fps

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
	float GetFps();

	int getCaptureDesiredFinalWidth();
	int getCaptureDesiredFinalHeight();

    REFERENCE_TIME lastFrame_ = 0;
    int64_t time_offset_dts_ns_ = 0;
    int64_t time_offset_pts_ns_ = 0;
    TimeOffsetType time_offset_type_ = TIME_OFFSET_NONE;

    uint64_t frame_start_ = 0;
    uint64_t frame_total_cnt_ = 0;
    uint64_t frame_sent_cnt_ = 0;
    uint64_t frame_dropped_cnt_ = 0;
    uint64_t first_frame_ms_ = 0;
    long double frame_processing_time_ms_ = 0.0;

    wchar_t out_[1024];

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
    HRESULT FillBufferFromShMem(IMediaSample *pSample, REFERENCE_TIME *startFrame, REFERENCE_TIME *endFrame, BOOL *discontinuity);

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

};

#endif
