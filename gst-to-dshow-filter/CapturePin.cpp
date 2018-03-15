#include <streams.h>

#include <tchar.h>
#include "Capture.h"
#include "names_and_ids.h"
#include "DibHelper.h"
#include <wmsdkidl.h>
#include "Logging.h"
#include "CommonTypes.h"
#include "d3d11.h"
#include "d3d11_3.h"
#include <dxgi.h>
#include <Psapi.h>
#include "libyuv/convert.h"
#include "windows.h"  

#ifndef MIN
#define MIN(a,b)  ((a) < (b) ? (a) : (b))  // danger! can evaluate "a" twice.
#endif

#define NS_TO_REFERENCE_TIME(t) (t)/100

#ifdef _DEBUG 
int show_performance = 1;
#else
int show_performance = 0;
#endif

// the default child constructor...
CPushPinDesktop::CPushPinDesktop(HRESULT *phr, CGameCapture *pFilter)
	: CSourceStream(NAME("Push Source CPushPinDesktop child/pin"), phr, pFilter, L"Capture"),
	m_pParent(pFilter)
{
	info("CPushPinDesktop");

	registry.Open(HKEY_CURRENT_USER, L"Software\\Bebo\\GstCapture", KEY_READ);

	// now read some custom settings...
	WarmupCounter();

}

CPushPinDesktop::~CPushPinDesktop()
{
    // FIXME release resources...
}

HRESULT CPushPinDesktop::Inactive(void) {
	active = false;
    info("STATS: %s", out_);
	return CSourceStream::Inactive();
};

HRESULT CPushPinDesktop::Active(void) {
	active = true;
	return CSourceStream::Active();
};


const static D3D_FEATURE_LEVEL d3d_feature_levels[] =
{
  D3D_FEATURE_LEVEL_11_0,
  D3D_FEATURE_LEVEL_10_1,
  D3D_FEATURE_LEVEL_10_0,
  D3D_FEATURE_LEVEL_9_3,
};

HRESULT CPushPinDesktop::CreateDeviceD3D11(IDXGIAdapter *adapter) {

  ComPtr<ID3D11Device> device;

  /* D3D_FEATURE_LEVEL level_used = D3D_FEATURE_LEVEL_9_3; */
  D3D_FEATURE_LEVEL level_used = D3D_FEATURE_LEVEL_10_1;

  HRESULT hr = D3D11CreateDevice(
    adapter,
    D3D_DRIVER_TYPE_UNKNOWN,
    NULL,
    D3D11_CREATE_DEVICE_DEBUG | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
    d3d_feature_levels,
    sizeof(d3d_feature_levels) / sizeof(D3D_FEATURE_LEVEL),
    D3D11_SDK_VERSION,
    &device,
    &level_used,
    &d3d_context_);
  info("CreateDevice HR: 0x%08x, level_used: 0x%08x (%d)", hr,
    (unsigned int)level_used, (unsigned int)level_used);
  device.As(&d3d_device3_);
  return S_OK;
}

HRESULT CPushPinDesktop::FillBuffer(IMediaSample *pSample)
{
	CheckPointer(pSample, E_POINTER);

    REFERENCE_TIME startFrame;
    REFERENCE_TIME endFrame;
    BOOL discontinuity;

	boolean gotFrame = false;
  HANDLE dxgi_handle = NULL;
	while (!gotFrame) {
		if (!active) {
			info("FillBuffer - inactive");
			return S_FALSE;
		}

		int code = FillBufferFromShMem(&dxgi_handle, &startFrame, &endFrame, &discontinuity);


		if (code == S_OK) {
			gotFrame = true;
		} else if (code == 2) { // not initialized yet
			gotFrame = false;
			continue;
		} else if (code == 3) { // black frame
			gotFrame = false;
			continue;
		} else {
			gotFrame = false;
		}
  }
	
  if (gotFrame) {

    if (!d3d_context_.Get()) {
      ComPtr<IDXGIFactory2> dxgiFactory;
      HRESULT hr = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, __uuidof(IDXGIFactory2), (void**)(dxgiFactory.GetAddressOf()));

      LUID luid;
      dxgiFactory->GetSharedResourceAdapterLuid(dxgi_handle, &luid);

      UINT index = 0;
      IDXGIAdapter* adapter = NULL;
      while (SUCCEEDED(dxgiFactory->EnumAdapters(index, &adapter)))
      {
        DXGI_ADAPTER_DESC desc;
        adapter->GetDesc(&desc);
        if (desc.AdapterLuid.LowPart == luid.LowPart && desc.AdapterLuid.HighPart == luid.HighPart) {
          // Identified a matching adapter.
          break;
        }
        adapter->Release();
        adapter = NULL;
        index++;
      }
      // At this point, if pAdapter is non-null, you identified an adapter that 
      // can open the shared resource.

      //TODO check result:
      CreateDeviceD3D11(adapter);
    }

    ComPtr<ID3D11Texture2D> resource;
    //ComPtr<ID3D11Resource> resource;

    HRESULT hr = d3d_device3_->OpenSharedResource(
      dxgi_handle,
      __uuidof(ID3D11Texture2D),
      (void**)resource.GetAddressOf());
    if (hr != S_OK) {
      error("OpenSharedResource failed %p", hr);
    }

    D3D11_TEXTURE2D_DESC desc = { 0 };
    D3D11_TEXTURE2D_DESC share_desc = { 0 };
    resource->GetDesc(&share_desc);
    desc.Format = share_desc.Format;
    desc.Width = share_desc.Width;
    desc.Height = share_desc.Height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
    desc.MiscFlags = 0;

    ComPtr<ID3D11Texture2D> copy_texture;
    d3d_device3_->CreateTexture2D(&desc, NULL, copy_texture.GetAddressOf());

    d3d_context_->CopyResource(copy_texture.Get(), resource.Get());

    // FIXME - need to do the map 3 frames behind otherwise we kill cpu/gpu performance
    // https://msdn.microsoft.com/en-us/library/windows/desktop/bb205132(v=vs.85).aspx#Performance_Considerations

    D3D11_MAPPED_SUBRESOURCE cpu_mem = { 0 };
    hr = d3d_context_->Map(copy_texture.Get(), 0, D3D11_MAP_READ, 0, &cpu_mem);
    if (hr != S_OK) {
      error("d3dcontext->Map failed %p", hr);
    }
    //DXGI_SURFACE_DESC frame_desc = { 0 };
    //DXGI_MAPPED_RECT map;
    //surface->GetDesc(&frame_desc);
    //surface->Map(&map, D3D11_MAP_READ);

    BYTE *pdata;
    pSample->GetPointer(&pdata);

    debug("SIZE: %d", pSample->GetSize());
    memcpy(pdata, cpu_mem.pData, pSample->GetSize());

#if 0

    //cpu_mem.RowPitch;
    int stride_y = shmem_->video_info.width;
    int stride_u = (shmem_->video_info.width + 1) / 2;
    int stride_v = stride_u;

    uint8* pdata_y = pdata;
    uint8* pdata_u = pdata + (shmem_->video_info.width * shmem_->video_info.height);
    uint8* pdata_v = pdata_u + ((shmem_->video_info.width * shmem_->video_info.height) >> 2);

    uint8* gst_y = (uint8*)cpu_mem.pData;
    uint8* gst_u = gst_y + (shmem_->video_info.width * shmem_->video_info.height);
    uint8* gst_v = gst_u + ((shmem_->video_info.width * shmem_->video_info.height) >> 2);

    libyuv::I420Copy(
      gst_y,
      stride_y,
      gst_u,
      stride_u,
      gst_v,
      stride_v,
      pdata_y,
      stride_y,
      pdata_u,
      stride_u,
      pdata_v,
      stride_v,
      shmem_->video_info.width,
      shmem_->video_info.height);
#endif

    //__uuidof(ID3D11Texture2D),

    d3d_context_->Unmap(copy_texture.Get(), 0);
  }
	pSample->SetTime((REFERENCE_TIME *)&startFrame, (REFERENCE_TIME *)&endFrame);

	// Set TRUE on every sample for uncompressed frames http://msdn.microsoft.com/en-us/library/windows/desktop/dd407021%28v=vs.85%29.aspx
	pSample->SetSyncPoint(TRUE);
    pSample->SetDiscontinuity(discontinuity);

	return S_OK;
}


HRESULT CPushPinDesktop::OpenShmMem()
{
    if (shmem_ != nullptr) {
       return S_OK;
    }

    shmem_new_data_semaphore_ = OpenSemaphore(SYNCHRONIZE, false, BEBO_SHMEM_DATA_SEM);
    if (!shmem_new_data_semaphore_) {
        // TODO: log only once...
        error("could not open semaphore mapping %d", GetLastError());
        Sleep(100);
        return -1;
    }

    shmem_handle_ = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, false, BEBO_SHMEM_NAME);
    if (!shmem_handle_) {
        error("could not create mapping %d", GetLastError());
        return -1;
    }

    shmem_mutex_ = OpenMutexW(SYNCHRONIZE, false, BEBO_SHMEM_MUTEX);
    WaitForSingleObject(shmem_mutex_, INFINITE); /// FIXME maybe timeout  == WAIT_OBJECT_0;

    shmem_ = (struct shmem*) MapViewOfFile(shmem_handle_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(struct shmem));
    if (!shmem_) {
        error("could not map shmem %d", GetLastError());
        ReleaseMutex(shmem_mutex_);
        return -1;
    }
    uint64_t shmem_size = shmem_->shmem_size;
    UnmapViewOfFile(shmem_);

    shmem_ = (struct shmem*) MapViewOfFile(shmem_handle_, FILE_MAP_ALL_ACCESS, 0, 0, shmem_size);

    shmem_->read_ptr = 0;
    // TODO read config from shared data!

    ReleaseMutex(shmem_mutex_);
    if (!shmem_) {
        error("could not map shmem %d", GetLastError());
        return -1;
    }
    info("Opened Shared Memory Buffer");
    return S_OK;
}


HRESULT CPushPinDesktop::FillBufferFromShMem(HANDLE * dxgi_handle, REFERENCE_TIME *startFrame, REFERENCE_TIME *endFrame, BOOL *discontinuity)
{
	//CheckPointer(pSample, E_POINTER);
    if (OpenShmMem() != S_OK) {
        return 2;
    }

    if (!shmem_) {
        return 2;
    }

    if (WaitForSingleObject(shmem_mutex_, INFINITE) == WAIT_OBJECT_0) {
        // FIXME handle all error cases
    }

    while (shmem_->write_ptr == 0 || shmem_->read_ptr >= shmem_->write_ptr) {

        DWORD result = SignalObjectAndWait(shmem_mutex_,
                                           shmem_new_data_semaphore_,
                                           200,
                                           false);
        if (result == WAIT_TIMEOUT) {
            debug("timed out after 200ms read_ptr: %d write_ptr: %d",
                shmem_->read_ptr,
                shmem_->write_ptr);
            return 2;
        } else if (result == WAIT_ABANDONED) {
            warn("semarphore is abandoned");
            return 2;
        } else if (result == WAIT_FAILED) {
            warn("semaphore wait failed 0x%010x", GetLastError());
            return 2;
        } else if (result != WAIT_OBJECT_0) {
            error("unknown semaphore event 0x%010x", result);
            return 2;
        } else  {
            if (WaitForSingleObject(shmem_mutex_, INFINITE) == WAIT_OBJECT_0) {
                continue;
            // FIXME handle all error cases
            }
        }
    }


	uint64_t start_processing = StartCounter();
    bool first_buffer = false;
    if (shmem_->read_ptr == 0) {
        first_buffer = true;
        frame_start_ = shmem_->write_ptr - 1;
        first_frame_ms_ = GetTickCount64();

        if (shmem_->write_ptr - shmem_->read_ptr > 1) {

            shmem_->read_ptr = shmem_->write_ptr - 1;
            debug("starting stream - resetting read pointer read_ptr: %d write_ptr: %d",
                shmem_->read_ptr, shmem_->write_ptr);
        }
    } else if (shmem_->write_ptr - shmem_->read_ptr > shmem_->count) {
        uint64_t read_ptr = shmem_->write_ptr - shmem_->count / 2;
        debug("late - resetting read pointer read_ptr: %d write_ptr: %d behind: %d new read_ptr: %d",
            shmem_->read_ptr, shmem_->write_ptr, shmem_->write_ptr-shmem_->read_ptr, read_ptr);
        shmem_->read_ptr = read_ptr;
        frame_late_cnt_++;
    } 

    uint64_t i = shmem_->read_ptr % shmem_->count;

    uint64_t frame_offset = shmem_->frame_offset + i * shmem_->frame_size;
    struct frame *frame = (struct frame*) (((char*)shmem_) + frame_offset);
    frame->ref_cnt++;
    *dxgi_handle = frame->dxgi_handle;

    CRefTime now;
    CSourceStream::m_pFilter->StreamTime(now);
    now = max(0, now); // can be negative before it started and negative will mess us up
    uint64_t now_in_ns = now * 100;
    int64_t time_offset_ns;

    if (first_buffer) {
        // TODO take start delta/behind into account
        // we prefer dts because it is monotonic
        if (frame->dts != GST_CLOCK_TIME_NONE) {
            time_offset_dts_ns_ = frame->dts - now_in_ns;
            time_offset_type_ = TIME_OFFSET_DTS;
            debug("using DTS as refernence delta in ns: %I64d", time_offset_dts_ns_);
        }
        if (frame->pts != GST_CLOCK_TIME_NONE) {
            time_offset_pts_ns_ = frame->pts - now_in_ns;
            if (time_offset_type_ == TIME_OFFSET_NONE) {
                time_offset_type_ = TIME_OFFSET_PTS;
                warn("using PTS as reference delta in ns: %I64d", time_offset_pts_ns_);
            } else {
                debug("PTS as delta in ns: %I64d", time_offset_pts_ns_);
            }
        }
    }

    bool have_time = false;
    if (time_offset_type_ == TIME_OFFSET_DTS) {
        if (frame->dts != GST_CLOCK_TIME_NONE) {
            *startFrame = NS_TO_REFERENCE_TIME(frame->dts - time_offset_dts_ns_);
            have_time = true;
        } else {
            warn("missing DTS timestamp");
        }
    } else if (time_offset_type_ == TIME_OFFSET_PTS || !have_time) {
        if (frame->pts != GST_CLOCK_TIME_NONE) {
            *startFrame = NS_TO_REFERENCE_TIME(frame->pts - time_offset_pts_ns_);
            have_time = true;
        } else {
            warn("missing PTS timestamp");
        }
    }

    uint64_t duration_ns = frame->duration;
    if (duration_ns == GST_CLOCK_TIME_NONE) {
        warn("missing duration timestamp");
        // FIXME: fake duration if we don't get it
    }

    if (!have_time) {
        *startFrame = lastFrame_ + NS_TO_REFERENCE_TIME(duration_ns);
    }

    if (lastFrame_ != 0 && lastFrame_ >= *startFrame) {
        warn("timestamp not monotonic last: %dI64t new: %dI64t", lastFrame_, *startFrame);
        // fake it
        *startFrame = lastFrame_ + NS_TO_REFERENCE_TIME(duration_ns) / 2;
    }
    

    *endFrame = *startFrame + NS_TO_REFERENCE_TIME(duration_ns);
    lastFrame_ = *startFrame;

    *discontinuity = first_buffer || frame->discontinuity ? true : false;


#if 1
    debug("dxgi_handle: %p pts: %lld i: %d size: %d read_ptr: %d write_ptr: %d behind: %d",
        frame->dxgi_handle,
        frame->pts,
        i,
        shmem_->read_ptr,
        shmem_->write_ptr,
        shmem_->write_ptr - shmem_->read_ptr);
#endif

#if 0
    BYTE *pdata;
    pSample->GetPointer(&pdata);

    int stride_y = shmem_->video_info.width;
    int stride_u = (shmem_->video_info.width + 1) / 2;
    int stride_v = stride_u;

    uint8* pdata_y = pdata;
    uint8* pdata_u = pdata + (shmem_->video_info.width * shmem_->video_info.height);
    uint8* pdata_v = pdata_u + ((shmem_->video_info.width * shmem_->video_info.height) >> 2);

    uint8* gst_y = data;
    uint8* gst_u = data + (shmem_->video_info.width * shmem_->video_info.height);
    uint8* gst_v = gst_u + ((shmem_->video_info.width * shmem_->video_info.height) >> 2);

    libyuv::I420Copy(
            gst_y,
            stride_y,
            gst_u,
            stride_u,
            gst_v,
            stride_v,
            pdata_y,
            stride_y,
            pdata_u,
            stride_u,
            pdata_v,
            stride_v,
            shmem_->video_info.width,
            shmem_->video_info.height);
#endif

    shmem_->read_ptr++;
    uint64_t read_ptr = shmem_->read_ptr;

    ReleaseMutex(shmem_mutex_);

    frame_sent_cnt_++;
    frame_total_cnt_ = read_ptr - frame_start_;
    frame_dropped_cnt_ = frame_total_cnt_ - frame_sent_cnt_;

    double avg_fps = 1000.0 * frame_sent_cnt_ / (GetTickCount64() - first_frame_ms_);

    long double processing_time_ms = GetCounterSinceStartMillis(start_processing);
    frame_processing_time_ms_ += processing_time_ms;
    
    _swprintf(out_, L"stats: total frames: %I64d dropped: %I64d pct_dropped: %.02f late: %I64d ave_fps: %.02f negotiated fps: %.03f avg proc time: %.03f",
        frame_total_cnt_,
        frame_dropped_cnt_,
        100.0 * frame_dropped_cnt_ / frame_total_cnt_,
        frame_late_cnt_,
        avg_fps,
        GetFps(),
        frame_processing_time_ms_/frame_sent_cnt_
    );

	return S_OK;
}

float CPushPinDesktop::GetFps() {
	return (float)(UNITS / m_rtFrameLength);
}

int CPushPinDesktop::getNegotiatedFinalWidth() {
	return m_iCaptureConfigWidth;
}

int CPushPinDesktop::getNegotiatedFinalHeight() {
	return m_iCaptureConfigHeight;
}

int CPushPinDesktop::getCaptureDesiredFinalWidth() {
	debug("getCaptureDesiredFinalWidth: %d", m_iCaptureConfigWidth);
	return m_iCaptureConfigWidth; // full/config setting, static
}

int CPushPinDesktop::getCaptureDesiredFinalHeight() {
	debug("getCaptureDesiredFinalHeight: %d", m_iCaptureConfigHeight);
	return m_iCaptureConfigHeight; // defaults to full/config static
}

//
// DecideBufferSize
//
// This will always be called after the format has been sucessfully
// negotiated (this is negotiatebuffersize). So we have a look at m_mt to see what size image we agreed.
// Then we can ask for buffers of the correct size to contain them.
//
HRESULT CPushPinDesktop::DecideBufferSize(IMemAllocator *pAlloc,
	ALLOCATOR_PROPERTIES *pProperties)
{
	//DebugBreak();
	CheckPointer(pAlloc, E_POINTER);
	CheckPointer(pProperties, E_POINTER);

	CAutoLock cAutoLock(m_pFilter->pStateLock());
	HRESULT hr = NOERROR;

	VIDEOINFO *pvi = (VIDEOINFO *)m_mt.Format();
	BITMAPINFOHEADER header = pvi->bmiHeader;

	ASSERT_RETURN(header.biPlanes == 1); // sanity check
	// ASSERT_RAISE(header.biCompression == 0); // meaning "none" sanity check, unless we are allowing for BI_BITFIELDS [?] so leave commented out for now
	// now try to avoid this crash [XP, VLC 1.1.11]: vlc -vvv dshow:// :dshow-vdev="bebo-game-capture" :dshow-adev --sout  "#transcode{venc=theora,vcodec=theo,vb=512,scale=0.7,acodec=vorb,ab=128,channels=2,samplerate=44100,audio-sync}:standard{access=file,mux=ogg,dst=test.ogv}" with 10x10 or 1000x1000
	// LODO check if biClrUsed is passed in right for 16 bit [I'd guess it is...]
	// pProperties->cbBuffer = pvi->bmiHeader.biSizeImage; // too small. Apparently *way* too small.

	int bytesPerLine;
	// there may be a windows method that would do this for us...GetBitmapSize(&header); but might be too small for VLC? LODO try it :)
	// some pasted code...
	int bytesPerPixel = 32 / 8; // we convert from a 32 bit to i420, so need more space in this case

	bytesPerLine = header.biWidth * bytesPerPixel;
	/* round up to a dword boundary for stride */
	if (bytesPerLine & 0x0003)
	{
		bytesPerLine |= 0x0003;
		++bytesPerLine;
	}

	ASSERT_RETURN(header.biHeight > 0); // sanity check
	ASSERT_RETURN(header.biWidth > 0); // sanity check
	// NB that we are adding in space for a final "pixel array" (http://en.wikipedia.org/wiki/BMP_file_format#DIB_Header_.28Bitmap_Information_Header.29) even though we typically don't need it, this seems to fix the segfaults
	// maybe somehow down the line some VLC thing thinks it might be there...weirder than weird.. LODO debug it LOL.
	int bitmapSize = 14 + header.biSize + (long)(bytesPerLine)*(header.biHeight) + bytesPerLine*header.biHeight;
	//pProperties->cbBuffer = header.biHeight * header.biWidth * 3 / 2; // necessary to prevent an "out of memory" error for FMLE. Yikes. Oh wow yikes.
	pProperties->cbBuffer = header.biHeight * header.biWidth * bytesPerPixel; // necessary to prevent an "out of memory" error for FMLE. Yikes. Oh wow yikes.

	pProperties->cBuffers = 1; // 2 here doesn't seem to help the crashes...

	// Ask the allocator to reserve us some sample memory. NOTE: the function
	// can succeed (return NOERROR) but still not have allocated the
	// memory that we requested, so we must check we got whatever we wanted.
	ALLOCATOR_PROPERTIES Actual;
	hr = pAlloc->SetProperties(pProperties, &Actual);
	if (FAILED(hr))
	{
		return hr;
	}

	// Is this allocator unsuitable?
	if (Actual.cbBuffer < pProperties->cbBuffer)
	{
		return E_FAIL;
	}

	return NOERROR;
} // DecideBufferSize
