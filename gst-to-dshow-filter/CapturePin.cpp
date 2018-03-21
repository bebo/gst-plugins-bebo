#include "Capture.h"

#include <tchar.h>
#include <wmsdkidl.h>
#include "allocator.h"
#include "DibHelper.h"
#include "Logging.h"

#ifndef MIN
#define MIN(a,b)  ((a) < (b) ? (a) : (b))  // danger! can evaluate "a" twice.
#endif

#define NS_TO_REFERENCE_TIME(t) (t)/100
#define GPU_WAIT_FRAME_COUNT 3

#ifdef _DEBUG 
int show_performance = 1;
#else
int show_performance = 0;
#endif

const static D3D_FEATURE_LEVEL kD3DFeatureLevels[] =
{
  D3D_FEATURE_LEVEL_11_0,
  D3D_FEATURE_LEVEL_10_1,
  D3D_FEATURE_LEVEL_10_0,
  D3D_FEATURE_LEVEL_9_3,
};

// the default child constructor...
CPushPinDesktop::CPushPinDesktop(HRESULT *phr, CGameCapture *pFilter)
  : CSourceStream(NAME("Push Source CPushPinDesktop child/pin"), phr, pFilter, L"Capture"),
  m_pParent(pFilter),
  m_rtFrameLength(UNITS / 60)
{
  info("CPushPinDesktop");

  registry.Open(HKEY_CURRENT_USER, L"Software\\Bebo\\GstCapture", KEY_READ);

  // now read some custom settings...
  WarmupCounter();

  // TODO: we should do it when we get it from shmem
  InitializeDXGI();
}

CPushPinDesktop::~CPushPinDesktop()
{
  // FIXME release resources...
}

HRESULT CPushPinDesktop::Inactive(void) {
  active = false;
  info("STATS: %s", out_);
  return CSourceStream::Inactive();
}

HRESULT CPushPinDesktop::Active(void) {
  active = true;
  return CSourceStream::Active();
}

HRESULT CPushPinDesktop::InitAllocator(IMemAllocator** ppAllocator)
{
  HRESULT hr = S_OK;

  IMemAllocator* alloc = new BeboAllocator(
      L"BeboAllocator",
      NULL,
      &hr); 
  if (alloc == NULL) {
    return E_OUTOFMEMORY;
  }

  if (FAILED(hr)) {
    delete alloc;
    return E_NOINTERFACE;
  }

  hr = alloc->QueryInterface(IID_IMemAllocator, (void **)ppAllocator);
  if (FAILED(hr)) {
    delete alloc;
    return E_NOINTERFACE;
  }

  return hr;
}

// -------------------------------------------------------------------------
// DecideAllocator
// We override the base stream negotiation to deny downstream pins request and
// supply our own allocator
//
HRESULT CPushPinDesktop::DecideAllocator(IMemInputPin* pPin, IMemAllocator** ppAlloc)
{
  HRESULT hr = NOERROR;
  *ppAlloc = NULL;

  // get downstream prop request
  // the derived class may modify this in DecideBufferSize, but
  // we assume that he will consistently modify it the same way,
  // so we only get it once
  ALLOCATOR_PROPERTIES prop;
  ZeroMemory(&prop, sizeof(prop));

  // whatever he returns, we assume prop is either all zeros
  // or he has filled it out.
  pPin->GetAllocatorRequirements(&prop);

  // if he doesn't care about alignment, then set it to 1
  if (prop.cbAlign == 0)
  {
    prop.cbAlign = 1;
  }

  // *** We ignore input pins allocator because WE make the frame buffers ***

  // Try the custom allocator
  hr = InitAllocator(ppAlloc);
  if (SUCCEEDED(hr))
  {
    // note - the properties passed here are in the same
    // structure as above and may have been modified by
    // the previous call to DecideBufferSize
    hr = DecideBufferSize(*ppAlloc, &prop);
    if (SUCCEEDED(hr))
    {
      hr = pPin->NotifyAllocator(*ppAlloc, FALSE);
      if (SUCCEEDED(hr))
      {
        return NOERROR;
      }
    }
  }

  // Likewise we may not have an interface to release
  if (*ppAlloc)
  {
    (*ppAlloc)->Release();
    *ppAlloc = NULL;
  }
  return hr;
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

HRESULT CPushPinDesktop::InitializeDXGI() {
  ComPtr<IDXGIFactory2> dxgi_factory;
  UINT flags = 0;

#ifdef _DEBUG
  flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

  HRESULT hr = CreateDXGIFactory2(flags, __uuidof(IDXGIFactory2), 
      (void**)(dxgi_factory.GetAddressOf()));

  // TODO: we need to get the right adapter but, we don't have access to dxgi shared handle atm
  // cuz it's in dxgi_frame
#if 0
  LUID luid;
  dxgiFactory->GetSharedResourceAdapterLuid(dxgi_frame->dxgi_handle, &luid);

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
    index++;
  }
  adapter->Release();
#endif

  //TODO handle result
  hr = CreateDeviceD3D11(NULL);
  return hr;
}

HRESULT CPushPinDesktop::CreateDeviceD3D11(IDXGIAdapter *adapter) 
{
  D3D_FEATURE_LEVEL level_used = D3D_FEATURE_LEVEL_9_3;

  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#ifdef _DEBUG
  flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  D3D_DRIVER_TYPE driver_type = (adapter == NULL) ? D3D_DRIVER_TYPE_HARDWARE :
    D3D_DRIVER_TYPE_UNKNOWN;

  HRESULT hr = D3D11CreateDevice(
      adapter,
      driver_type,
      NULL,
      flags,
      kD3DFeatureLevels,
      sizeof(kD3DFeatureLevels) / sizeof(D3D_FEATURE_LEVEL),
      D3D11_SDK_VERSION,
      &d3d_device_,
      &level_used,
      &d3d_context_);
  info("CreateDevice HR: 0x%08x, level_used: 0x%08x (%d)", hr,
      (unsigned int)level_used, (unsigned int)level_used);
  return S_OK;
}

HRESULT CPushPinDesktop::DoBufferProcessingLoop(void)
{
  Command com;

  OnThreadStartPlay();

  do {
    while (!CheckRequest(&com)) {
      IMediaSample *media_sample;

      HRESULT hr = GetDeliveryBuffer(&media_sample, NULL, NULL, 0);
      if (FAILED(hr)) {
        Sleep(1);
        continue;  // go round again. Perhaps the error will go away
        // or the allocator is decommited & we will be asked to
        // exit soon.
      }

      // Virtual function user will override.
      DxgiFrame* dxgi_frame = NULL;
      hr = FillBuffer(media_sample, &dxgi_frame);

      if (hr == S_OK) {
        hr = Deliver(media_sample);
        media_sample->Release();

        UnrefDxgiFrame(dxgi_frame);

        // downstream filter returns S_FALSE if it wants us to
        // stop or an error if it's reporting an error.
        if (hr != S_OK)
        {
          DbgLog((LOG_TRACE, 2, TEXT("Deliver() returned %08x; stopping"), hr));
          return S_OK;
        }

      } else if (hr == S_FALSE) {
        // derived class wants us to stop pushing data
        media_sample->Release();
        UnrefDxgiFrame(dxgi_frame);
        DeliverEndOfStream();
        return S_OK;
      } else {
        // derived class encountered an error
        media_sample->Release();
        UnrefDxgiFrame(dxgi_frame);
        DbgLog((LOG_ERROR, 1, TEXT("Error %08lX from FillBuffer!!!"), hr));
        DeliverEndOfStream();
        m_pFilter->NotifyEvent(EC_ERRORABORT, hr, 0);
        return hr;
      }

      // all paths release the sample
    }

    // For all commands sent to us there must be a Reply call!

    if (com == CMD_RUN || com == CMD_PAUSE) {
      Reply(NOERROR);
    }
    else if (com != CMD_STOP) {
      Reply((DWORD)E_UNEXPECTED);
      DbgLog((LOG_ERROR, 1, TEXT("Unexpected command!!!")));
    }
  } while (com != CMD_STOP);

  return S_FALSE;
}

HRESULT CPushPinDesktop::FillBuffer(IMediaSample* media_sample) {
  error("FillBuffer SHOULD NOT BE CALLED");
  return E_NOTIMPL;
}

D3D11_TEXTURE2D_DESC CPushPinDesktop::ConvertToStagingTexture(D3D11_TEXTURE2D_DESC share_desc) {
  D3D11_TEXTURE2D_DESC desc = { 0 };
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
  return desc;
}

HRESULT CPushPinDesktop::FillBuffer(IMediaSample* media_sample, DxgiFrame** out_dxgi_frame)
{
  CheckPointer(media_sample, E_POINTER);

  REFERENCE_TIME start_frame;
  REFERENCE_TIME end_frame;
  bool discontinuity;
  bool got_frame = false;
  DWORD wait_time = 1;

  while (!got_frame) {
    if (!active) {
      info("FillBuffer - inactive");
      return S_FALSE;
    }

    DxgiFrame* dxgi_frame = new DxgiFrame();

    // TODO CANT WAIT 202ms if you have frames in the queue
    int code = FillBufferFromShMem(dxgi_frame, wait_time);

    if (code == S_OK) {
      got_frame = true;
    } else if (code == 2) { // not initialized yet
      got_frame = false;
      continue;
    } else if (code == 3) { // no new frame, but timeout
      got_frame = false;
      // clean up 
    } else {
      got_frame = false;
      continue;
    }

    if (got_frame) {
      CopyTextureToStagingQueue(dxgi_frame);
    }

    DWORD wait_time_ms = GetReadyFrameFromQueue(&dxgi_frame);
    if (wait_time_ms == 0) {
      PushFrameToMediaSample(dxgi_frame, media_sample);
      *out_dxgi_frame = dxgi_frame;
      break;
    } else {
      got_frame = false;
      wait_time = wait_time_ms;
    }
  }

  // Set TRUE on every sample for uncompressed frames 
  // http://msdn.microsoft.com/en-us/library/windows/desktop/dd407021%28v=vs.85%29.aspx
  media_sample->SetSyncPoint(TRUE);
  media_sample->SetDiscontinuity((*out_dxgi_frame)->discontinuity);
  media_sample->SetTime(&(*out_dxgi_frame)->start_time, &(*out_dxgi_frame)->end_time);

  return S_OK;
}

DWORD CPushPinDesktop::GetReadyFrameFromQueue(DxgiFrame** out_frame) {
  if (dxgi_frame_queue_.size() == 0) {
    return 201;
  }

  DxgiFrame* frame = dxgi_frame_queue_.front();

  uint64_t age = GetCounterSinceStartMillis(frame->sent_gpu_time);
  uint64_t wait_from_gpu_duration_ms = (GPU_WAIT_FRAME_COUNT * frame->frame_length / 10000L);

  if (age >= wait_from_gpu_duration_ms) { // the frame is old enough
    debug("%S EXPIRED texture age: %llu, frame ts: %llu, wait frame length: %llu, size: %d, nr: %llu", 
        __func__,
        age,
        frame->sent_gpu_time, 
        wait_from_gpu_duration_ms,
        dxgi_frame_queue_.size(),
        frame->nr);
    *out_frame = frame;
    dxgi_frame_queue_.pop();
    return 0;
  }

  uint64_t wait_time_ms = max(1, wait_from_gpu_duration_ms - age);
  debug("%S WAIT age: %llu, frame ts: %llu, size: %d, wait frame length : %llu, wait_time_ms: %llu, nr: %llu", 
      __func__,
      age,
      frame->sent_gpu_time,
      dxgi_frame_queue_.size(),
      wait_from_gpu_duration_ms,
      wait_time_ms,
      frame->nr);
  return (DWORD) wait_time_ms;
}

HRESULT CPushPinDesktop::CopyTextureToStagingQueue(DxgiFrame* frame) {
  ComPtr<ID3D11Texture2D> shared_texture;

  HRESULT hr = d3d_device_->OpenSharedResource(
      frame->dxgi_handle,
      __uuidof(ID3D11Texture2D),
      (void**)shared_texture.GetAddressOf());

  if (hr != S_OK) {
    // TODO: error handling, what to do here
    error("OpenSharedResource failed %p", hr);
  }

  D3D11_TEXTURE2D_DESC desc = { 0 };
  D3D11_TEXTURE2D_DESC share_desc = { 0 };

  shared_texture->GetDesc(&share_desc);

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

  d3d_device_->CreateTexture2D(&desc, NULL, &frame->texture);

  frame->sent_gpu_time = StartCounter();
  debug("%S QUEUE GPU texture before size: %d, nr: %llu", 
      __func__,
      dxgi_frame_queue_.size(),
      frame->nr);

  d3d_context_->CopyResource(frame->texture.Get(), shared_texture.Get());

  dxgi_frame_queue_.push(frame);
  return hr;
}

HRESULT CPushPinDesktop::PushFrameToMediaSample(DxgiFrame* frame, IMediaSample* media_sample) {
  D3D11_MAPPED_SUBRESOURCE cpu_mem = { 0 };

  // Unmap happens after we deliver the buffer
  HRESULT hr = d3d_context_->Map(frame->texture.Get(), 0, D3D11_MAP_READ, 0, &cpu_mem);
  if (hr != S_OK) {
    // TODO: error handling, WHAT TO DO?
    error("d3dcontext->Map failed %p", hr);
  }

  if (cpu_mem.DepthPitch == 0) {
    // should never happen, but i've seen weird things happened before.
    error("DepthPitch is 0, should not be possible, but here we are.");
  }

  frame->texture_mapped_to_memory = true;
  ((CMediaSample*)media_sample)->SetPointer((BYTE*)cpu_mem.pData, cpu_mem.DepthPitch);
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
  WaitForSingleObject(shmem_mutex_, INFINITE); // FIXME maybe timeout  == WAIT_OBJECT_0;

  shmem_ = (struct shmem*) MapViewOfFile(shmem_handle_, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(struct shmem));
  if (!shmem_) {
    error("could not map shmem %d", GetLastError());
    ReleaseMutex(shmem_mutex_);
    return -1;
  }
  uint64_t shmem_size = shmem_->shmem_size;
  uint64_t version = shmem_->version;
  UnmapViewOfFile(shmem_);
  shmem_ = nullptr;

  if (version != SHM_INTERFACE_VERSION) {
    ReleaseMutex(shmem_mutex_);
    error("SHM_INTERFACE_VERSION missmatch %d != %d", version, SHM_INTERFACE_VERSION);
    Sleep(3000);
    return -1;
  }

  shmem_ = (struct shmem*) MapViewOfFile(shmem_handle_, FILE_MAP_ALL_ACCESS, 0, 0, shmem_size);

  ReleaseMutex(shmem_mutex_);
  if (!shmem_) {
    error("could not map shmem %d", GetLastError());
    return -1;
  }


  // TODO read config from shared data!
  shmem_->read_ptr = 0;
  info("Opened Shared Memory Buffer");
  return S_OK;
}

HRESULT CPushPinDesktop::FillBufferFromShMem(DxgiFrame* dxgi_frame, DWORD wait_time_ms) {
  if (OpenShmMem() != S_OK) {
    return 2;
  }

  if (!shmem_) {
    return 2;
  }

  REFERENCE_TIME start_frame;
  REFERENCE_TIME end_frame; 
  bool discontinuity;

#if 1
  DWORD result = WaitForSingleObject(shmem_mutex_, INFINITE);

  // FIXME handle all error cases
  while (shmem_->write_ptr == 0 || shmem_->read_ptr >= shmem_->write_ptr) {
    DWORD result = SignalObjectAndWait(shmem_mutex_,
        shmem_new_data_semaphore_,
        wait_time_ms,
        false);

    if (result == WAIT_TIMEOUT) {
      debug("timed out after %dms read_ptr: %d write_ptr: %d",
          wait_time_ms,
          shmem_->read_ptr,
          shmem_->write_ptr);
      return 3;
    } else if (result == WAIT_ABANDONED) {
      warn("semarphore is abandoned");
      return 2;
    } else if (result == WAIT_FAILED) {
      warn("semaphore wait failed 0x%010x", GetLastError());
      return 2;
    } else if (result != WAIT_OBJECT_0) {
      error("unknown semaphore event 0x%010x", result);
      return 2;
    } else {
      if (WaitForSingleObject(shmem_mutex_, INFINITE) == WAIT_OBJECT_0) {
        continue;
        // FIXME handle all error cases
      }
    }
  }
#else
  DWORD result = 0;

  while ((result = WaitForSingleObject(shmem_mutex_, INFINITE)) == WAIT_OBJECT_0) {
    bool wait_for_semaphore = (shmem_->write_ptr == 0 || shmem_->read_ptr >= shmem_->write_ptr);
    ReleaseMutex(shmem_mutex_);

    if (!wait_for_semaphore) {
      break;
    }

    DWORD semaphore_result = WaitForSingleObject(shmem_new_data_semaphore_, 200);

    if (semaphore_result == WAIT_TIMEOUT) {
      debug("timed out after 200ms read_ptr: %d write_ptr: %d",
          0 /*shmem_->read_ptr*/,
          0 /*shmem_->write_ptr*/);
      return 2;
    } else if (semaphore_result == WAIT_ABANDONED) {
      warn("semaphore is abandoned");
      return 2;
    } else if (semaphore_result == WAIT_FAILED) {
      warn("semaphore wait failed 0x%010x", GetLastError());
      return 2;
    } else if (semaphore_result != WAIT_OBJECT_0) {
      error("unknown semaphore event 0x%010x", result);
      return 2;
    }
  }

  if (result != WAIT_OBJECT_0) {
    error("Failed to acquire shmem_mutex_, result: %d", result);
    ReleaseMutex(shmem_mutex_);
    return result == WAIT_TIMEOUT ? 3 : 2;
  }

  // semaphore signalled, gonna hold the shmem_mutex_
  WaitForSingleObject(shmem_mutex_, INFINITE);
#endif

  uint64_t start_processing = StartCounter();
  bool first_buffer = false;

  if (shmem_->read_ptr == 0) {
    first_buffer = true;
    frame_start_ = shmem_->write_ptr - 1;
    first_frame_ms_ = GetTickCount64();

    if (shmem_->write_ptr - shmem_->read_ptr > 0) {
      shmem_->read_ptr = shmem_->write_ptr;
      info("starting stream - resetting read pointer read_ptr: %d write_ptr: %d",
          shmem_->read_ptr, shmem_->write_ptr);
      UnrefBefore(shmem_->read_ptr);
    }
  }
  else if (shmem_->write_ptr - shmem_->read_ptr > shmem_->count) {
    uint64_t read_ptr = shmem_->write_ptr - shmem_->count / 2;
    info("late - resetting read pointer read_ptr: %d write_ptr: %d behind: %d new read_ptr: %d",
        shmem_->read_ptr, shmem_->write_ptr, shmem_->write_ptr - shmem_->read_ptr, read_ptr);
    //frame_late_cnt_++;
    shmem_->read_ptr = read_ptr;
    UnrefBefore(shmem_->read_ptr);
  }

  uint64_t i = shmem_->read_ptr % shmem_->count;

  uint64_t frame_offset = shmem_->frame_offset + i * shmem_->frame_size;
  struct frame *frame = (struct frame*) (((char*)shmem_) + frame_offset);

  frame->ref_cnt++;

  CRefTime now;
  CSourceStream::m_pFilter->StreamTime(now);
  now = max(0, now); // can be negative before it started and negative will mess us up
  uint64_t now_in_ns = now * 100;

  if (first_buffer) {
    uint64_t latency_ns = frame->duration * GPU_WAIT_FRAME_COUNT + frame->latency;
    info("calculated gst_latency: %d + gpu_latency: %d * %d = latency_ms: %d",frame->latency / 1000000, GPU_WAIT_FRAME_COUNT, frame->duration / 1000000, latency_ns / 1000000);
    // TODO take start delta/behind into account
    // we prefer dts because it is monotonic
    if (frame->dts != GST_CLOCK_TIME_NONE) {
      time_offset_dts_ns_ = frame->dts - now_in_ns + latency_ns;
      time_offset_type_ = TIME_OFFSET_DTS;
      debug("using DTS as refernence delta in ns: %I64d", time_offset_dts_ns_);
    }
    if (frame->pts != GST_CLOCK_TIME_NONE) {

      time_offset_pts_ns_ = frame->pts - now_in_ns + latency_ns;
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
      start_frame = NS_TO_REFERENCE_TIME(frame->dts - time_offset_dts_ns_);
      have_time = true;
    } else {
      warn("missing DTS timestamp");
    }
  } else if (time_offset_type_ == TIME_OFFSET_PTS || !have_time) {
    if (frame->pts != GST_CLOCK_TIME_NONE) {
      start_frame = NS_TO_REFERENCE_TIME(frame->pts - time_offset_pts_ns_);
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
    start_frame = last_frame_ + NS_TO_REFERENCE_TIME(duration_ns);
  }

  if (last_frame_ != 0 && last_frame_ >= start_frame) {
    warn("timestamp not monotonic last: %dI64t new: %dI64t", last_frame_, start_frame);
    // fake it
    start_frame = last_frame_ + NS_TO_REFERENCE_TIME(duration_ns) / 2;
  }


  end_frame = start_frame + NS_TO_REFERENCE_TIME(duration_ns);
  last_frame_ = start_frame;

  discontinuity = first_buffer || frame->discontinuity ? true : false;

  dxgi_frame->SetFrame(frame, i, start_frame, end_frame, discontinuity);

  debug("dxgi_handle: %p pts: %lld i: %d read_ptr: %d write_ptr: %d behind: %d",
      frame->dxgi_handle,
      frame->pts,
      i,
      shmem_->read_ptr,
      shmem_->write_ptr,
      shmem_->write_ptr - shmem_->read_ptr);

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
      frame_processing_time_ms_ / frame_sent_cnt_
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

struct frame * CPushPinDesktop::GetShmFrame(uint64_t i) {
  uint64_t frame_offset = shmem_->frame_offset + i * shmem_->frame_size;
  return (struct frame*) (((char*)shmem_) + frame_offset);
}

struct frame * CPushPinDesktop::GetShmFrame(DxgiFrame *dxgi_frame) {
  return GetShmFrame(dxgi_frame->index);
}

HRESULT CPushPinDesktop::UnrefBefore(uint64_t before) {
  //expect to hold mutex!
  for (int i = 0; i < shmem_->count; i++) {
    auto shmFrame = GetShmFrame(i);
    if (shmFrame->ref_cnt < 2 && shmFrame->nr < before) {
      info("unfref %d < %d", shmFrame->nr, before);
      shmFrame->ref_cnt = 0;
    }
  }
  return S_OK;
}

HRESULT CPushPinDesktop::UnrefDxgiFrame(DxgiFrame* dxgi_frame) {
  if (dxgi_frame == NULL) {
    return S_OK;
  }
  if (dxgi_frame->texture.Get()) {
    d3d_context_->Unmap(dxgi_frame->texture.Get(), 0);
  }

  if (WaitForSingleObject(shmem_mutex_, 1000) != WAIT_OBJECT_0) {
    error("didn't get lock");
    return S_FALSE;
  }

  auto shmFrame = GetShmFrame(dxgi_frame);
  // TODO: check it is the same frame!
  shmFrame->ref_cnt = shmFrame->ref_cnt - 2;

  debug("unref dxgi_handle: %p nr: %lld i: %d ref_cnt %d",
      dxgi_frame->dxgi_handle,
      dxgi_frame->nr,
      dxgi_frame->index,
      shmFrame->ref_cnt);
  ReleaseMutex(shmem_mutex_);
  delete dxgi_frame;
  return S_OK;
}

DxgiFrame::DxgiFrame() 
  : dxgi_handle(NULL), nr(0), index(0), 
    texture_mapped_to_memory(false), 
    discontinuity(false),
    start_time(0L),
    end_time(0L),
    sent_gpu_time(0L) {
}

DxgiFrame::~DxgiFrame() {
}

void DxgiFrame::SetFrame(struct frame *shmem_frame, uint64_t i,
    REFERENCE_TIME start_t, REFERENCE_TIME end_t, bool discont) {
  index = i;
  nr = shmem_frame->nr;
  start_time = start_t;
  end_time = end_t;
  discontinuity = discont;
  frame_length = NS_TO_REFERENCE_TIME(shmem_frame->duration);
  dxgi_handle = shmem_frame->dxgi_handle;
}
