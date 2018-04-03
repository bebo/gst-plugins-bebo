#include "Capture.h"

#include <tchar.h>
#include <wmsdkidl.h>
#include "allocator.h"
#include "DibHelper.h"
#include "Logging.h"

#ifndef MIN
#define MIN(a,b)  ((a) < (b) ? (a) : (b))  // danger! can evaluate "a" twice.
#endif

#define NS_TO_REFERENCE_TIME(t)           (t)/100
#define NS2MS(t)                          (t)/1000000
#define REFERENCE_TIME_TO_MS(t)           (t)/10000
#define GPU_WAIT_FRAME_COUNT              2
#define GPU_QUEUE_MAX_FRAME_COUNT         10
#define DEFAULT_WAIT_NEW_FRAME_TIME       200

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
    frame_pool_(NULL)
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
  if (frame_pool_) {
    delete frame_pool_;
  }
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

D3D11_TEXTURE2D_DESC CPushPinDesktop::ConvertToStagingTextureDesc(D3D11_TEXTURE2D_DESC share_desc) {
  D3D11_TEXTURE2D_DESC desc = { 0 };
  desc.Width = share_desc.Width;
  desc.Height = share_desc.Height;
  desc.Format = share_desc.Format;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_STAGING;
  desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
  desc.BindFlags = 0;
  desc.MiscFlags = 0;
  return desc;
}

HRESULT CPushPinDesktop::FillBuffer(IMediaSample* media_sample, DxgiFrame** out_dxgi_frame)
{
  CheckPointer(media_sample, E_POINTER);

  bool got_frame = false;
  uint64_t start_processing = StartCounter();

  while (!got_frame) {
    if (!active) {
      info("FillBuffer - inactive");
      return S_FALSE;
    }

    DxgiFrame* dxgi_frame = NULL;

    int64_t wait_time = GetNewFrameWaitTime();

    if (wait_time > -1) {
      HRESULT code = GetAndWaitForShmemFrame(&dxgi_frame, (DWORD) wait_time);

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
    }

    if (got_frame && ShouldDropNewFrame()) {
      debug("DROP frame from shmem, queue is filled. nr: %llu dxgi_handle: %llu wait_time: %lu", 
          dxgi_frame->nr, dxgi_frame->dxgi_handle, wait_time);
      UnrefDxgiFrame(dxgi_frame);
      got_frame = false;
    }

    // prioritize fill buffer to fill up the queue before start consuming from our own queue
    if (!got_frame &&
        dxgi_frame_queue_.size() < GPU_QUEUE_MAX_FRAME_COUNT) {
      HRESULT code = GetAndWaitForShmemFrame(&dxgi_frame, 0);
      got_frame = (code == S_OK);
    }

    if (got_frame) {
      CopyTextureToStagingQueue(dxgi_frame);
    }

    dxgi_frame = GetReadyFrameFromQueue();
    got_frame = (dxgi_frame != NULL);

    if (got_frame) {
      HRESULT hr = PushFrameToMediaSample(dxgi_frame, media_sample);
      if (hr != S_OK) { // retry
        got_frame = false;
        continue;
      }

      *out_dxgi_frame = dxgi_frame;
    }

    d3d_context_->Flush();
  }

  DxgiFrame* out_frame = *out_dxgi_frame;

  // wait if necessary to ensure we dont send frames every 1ms
  uint64_t frame_sent_diff_ms = (last_frame_sent_ms_ == 0) ? 0 :
    GetCounterSinceStartMillisRounded(last_frame_sent_ms_);
  uint64_t frame_length_ms = REFERENCE_TIME_TO_MS(out_frame->frame_length);
  uint64_t sleep_time_ms = 0;

  if (last_frame_sent_ms_ != 0 && 
      frame_sent_diff_ms < frame_length_ms) {
    sleep_time_ms = frame_length_ms - frame_sent_diff_ms;
    Sleep((DWORD) sleep_time_ms);

    // get the new sent_diff_ms
    frame_sent_diff_ms = (last_frame_sent_ms_ == 0) ? 0 :
      GetCounterSinceStartMillisRounded(last_frame_sent_ms_);
  }

  debug("DELIVER to dshow. nr: %llu dxgi_handle: %llu start_time: %lld end_time: %lld delta_time: %lld map_time: %llu sleep_time_ms: %llu frame_sent_diff: %lld", 
      out_frame->nr,
      out_frame->dxgi_handle,
      out_frame->start_time,
      out_frame->end_time,
      out_frame->end_time - out_frame->start_time,
      last_map_took_time_ms_,
      sleep_time_ms,
      frame_sent_diff_ms);

  last_frame_sent_ms_ = StartCounter();

  // stats
  frame_sent_cnt_++;
  frame_dropped_cnt_ = frame_total_cnt_ - frame_sent_cnt_;

  double avg_fps = 1000.0 * frame_sent_cnt_ / (GetTickCount64() - first_frame_ms_);

  long double processing_time_ms = GetCounterSinceStartMillis(start_processing);
  frame_processing_time_ms_ += processing_time_ms;

  _swprintf(out_, L"stats: total frames: %I64d dropped: %I64d duplicated: %llu pct_dropped: %.02f late: %I64d ave_fps: %.02f negotiated fps: %.03f avg proc time: %.03f",
      frame_total_cnt_,
      frame_dropped_cnt_,
      frame_duplicated_cnt_,
      100.0 * frame_dropped_cnt_ / frame_total_cnt_,
      frame_late_cnt_,
      avg_fps,
      0.0, // GetFps(),
      frame_processing_time_ms_ / frame_sent_cnt_
      );

  // Set TRUE on every sample for uncompressed frames 
  // http://msdn.microsoft.com/en-us/library/windows/desktop/dd407021%28v=vs.85%29.aspx
  media_sample->SetSyncPoint(TRUE);
  media_sample->SetDiscontinuity(out_frame->discontinuity);
  media_sample->SetTime(&out_frame->start_time, &out_frame->end_time);

  return S_OK;
}

bool CPushPinDesktop::ShouldDropNewFrame() {
  return (dxgi_frame_queue_.size() >= GPU_QUEUE_MAX_FRAME_COUNT);
}

int64_t CPushPinDesktop::GetNewFrameWaitTime() {
  if (dxgi_frame_queue_.size() == 0) {
    return DEFAULT_WAIT_NEW_FRAME_TIME;
  }

  DxgiFrame* frame = dxgi_frame_queue_.front();

  return REFERENCE_TIME_TO_MS(frame->frame_length);
}

DxgiFrame* CPushPinDesktop::GetReadyFrameFromQueue() {
  if (dxgi_frame_queue_.size() == 0) {
    return NULL;
  }

  DxgiFrame* frame = dxgi_frame_queue_.front();
  uint64_t age = GetCounterSinceStartMillisRounded(frame->sent_to_gpu_time);

  if (dxgi_frame_queue_.size() >= GPU_WAIT_FRAME_COUNT) {
    dxgi_frame_queue_.pop_front();

    debug("POPPED texture from gpu queue. nr: %llu dxgi_handle: %llu new_queue_size: %d age: %lld", 
        frame->nr,
        frame->dxgi_handle,
        dxgi_frame_queue_.size(),
        age);
    return frame;
  }

  return NULL;
}

HRESULT CPushPinDesktop::CopyTextureToStagingQueue(DxgiFrame* frame) {
  ComPtr<ID3D11Texture2D> shared_texture;

  HRESULT hr = d3d_device_->OpenSharedResource(
      frame->dxgi_handle,
      __uuidof(ID3D11Texture2D),
      (void**)shared_texture.GetAddressOf());

  if (hr != S_OK) {
      debug("DROP frame cause failed to open shared handle. nr: %llu dxgi_handle: %llu", 
          frame->nr, frame->dxgi_handle);
    UnrefDxgiFrame(frame);
    return hr;
  }

  // only create a texture if there's no texture already
  if (frame->texture.Get() == NULL) {
    D3D11_TEXTURE2D_DESC share_desc = { 0 };
    shared_texture->GetDesc(&share_desc);

    D3D11_TEXTURE2D_DESC desc = ConvertToStagingTextureDesc(share_desc);

    d3d_device_->CreateTexture2D(&desc, NULL, &frame->texture);
  }

  frame->sent_to_gpu_time = StartCounter();
  d3d_context_->CopyResource(frame->texture.Get(), shared_texture.Get());

  dxgi_frame_queue_.push_back(frame);

  debug("PUSHED texture to gpu queue. nr: %llu dxgi_handle: %llu new_queue_size: %d", 
      frame->nr,
      frame->dxgi_handle,
      dxgi_frame_queue_.size());

  return hr;
}

HRESULT CPushPinDesktop::PushFrameToMediaSample(DxgiFrame* frame, IMediaSample* media_sample) {
  D3D11_MAPPED_SUBRESOURCE cpu_mem = { 0 };

  int64_t now = StartCounter();

  HRESULT hr;

  // note: Unmap happens after we deliver the buffer, in UnrefDxgiFrame
  hr = d3d_context_->Map(frame->texture.Get(), 0,
      D3D11_MAP_READ,
      0,
      &cpu_mem);

  last_map_took_time_ms_ = GetCounterSinceStartMillisRounded(now);

  if (hr != S_OK) {
    if (hr != DXGI_ERROR_WAS_STILL_DRAWING) {
      error("MAP failed: 0x%016x", hr);
    }

    debug("DROP frame from failed to map from gpu to cpu. nr: %llu dxgi_handle: %llu map_time: %lu", frame->nr, frame->dxgi_handle, last_map_took_time_ms_);
    UnrefDxgiFrame(frame);
    return hr;
  }

  frame->mapped_into_cpu = true;

  ((CMediaSample*)media_sample)->SetPointer((BYTE*)cpu_mem.pData, cpu_mem.DepthPitch);

#if _DEBUG
  static BYTE* lf_data = new BYTE[1280 * 720 * 4];
  if (memcmp(lf_data, cpu_mem.pData, cpu_mem.DepthPitch) == 0) {
    frame_duplicated_cnt_++;
    debug("DUPLICATE frame. nr: %llu dxgi_handle: %llu map_time: %lu", frame->nr, frame->dxgi_handle, last_map_took_time_ms_);
  }
  memcpy(lf_data, cpu_mem.pData, cpu_mem.DepthPitch);
#endif

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

  // TODO read config from shared data!
  shmem_->read_ptr = 0;
  uint64_t shmem_count = shmem_->count;

  ReleaseMutex(shmem_mutex_);
  if (!shmem_) {
    error("could not map shmem %d", GetLastError());
    return -1;
  }


  frame_pool_ = new DxgiFramePool((int)shmem_count);
  info("Opened Shared Memory Buffer");
  return S_OK;
}

HRESULT CPushPinDesktop::GetAndWaitForShmemFrame(DxgiFrame** out_dxgi_frame, DWORD wait_time_ms) {
  if (OpenShmMem() != S_OK) {
    return 2;
  }

  if (!shmem_) {
    return 2;
  }

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
  bool first_buffer = false;

  if (shmem_->read_ptr == 0 ) {
    first_buffer = true;
    frame_start_ = shmem_->write_ptr - 1;
    first_frame_ms_ = GetTickCount64();

    if (shmem_->write_ptr - shmem_->read_ptr > 0) {
      shmem_->read_ptr = shmem_->write_ptr;
      info("starting stream - resetting read pointer read_ptr: %d write_ptr: %d",
          shmem_->read_ptr, shmem_->write_ptr);
      UnrefBefore(shmem_->read_ptr);
    }
  } else if (shmem_->write_ptr - shmem_->read_ptr > shmem_->count) {
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
  int64_t now_in_ns = now * 100;

  if (first_buffer) {
    debug("now: %lld", now);

    uint64_t delay_frame_count = GPU_WAIT_FRAME_COUNT;
    uint64_t latency_ns = frame->duration * delay_frame_count + frame->latency;
    info("calculated gst_latency: %llu + gpu_latency: %d * %d = latency_ms: %d",
      NS2MS(frame->latency), delay_frame_count, NS2MS(frame->duration), NS2MS(latency_ns));

    // TODO take start delta/behind into account
    // we prefer dts because it is monotonic
    if (frame->dts != GST_CLOCK_TIME_NONE) {
      int64_t dts = frame->dts;
      time_offset_dts_ns_ = dts - now_in_ns + latency_ns;
      time_offset_type_ = TIME_OFFSET_DTS;
      debug("using DTS as refernence delta in ns: %lld", time_offset_dts_ns_);
    }
    if (frame->pts != GST_CLOCK_TIME_NONE) {
      int64_t pts = frame->pts;
      time_offset_pts_ns_ = frame->pts - now_in_ns + latency_ns;
      if (time_offset_type_ == TIME_OFFSET_NONE) {
        time_offset_type_ = TIME_OFFSET_PTS;
        warn("using PTS as reference delta in ns: %lld", time_offset_pts_ns_);
      } else {
        debug("PTS as delta in ns: %lld", time_offset_pts_ns_);
      }
    }
  }
  REFERENCE_TIME start_frame;
  REFERENCE_TIME end_frame; 
  bool discontinuity;
  bool have_time = false;
  if (time_offset_type_ == TIME_OFFSET_DTS) {
    if (frame->dts != GST_CLOCK_TIME_NONE) {
      int64_t dts = frame->pts;
      start_frame = NS_TO_REFERENCE_TIME(dts - time_offset_dts_ns_);
      have_time = true;
    } else {
      warn("missing DTS timestamp");
    }
  } else if (time_offset_type_ == TIME_OFFSET_PTS || !have_time) {
    if (frame->pts != GST_CLOCK_TIME_NONE) {
      int64_t pts = frame->pts;
      start_frame = NS_TO_REFERENCE_TIME(pts - time_offset_pts_ns_);
      have_time = true;
    } else {
      warn("missing PTS timestamp");
    }
  }

  debug("MS nr: %llu pts:%llu - offset: %lld = start_frame: %lld",
    frame->nr,
    NS2MS(frame->pts),
    NS2MS(time_offset_pts_ns_),
    REFERENCE_TIME_TO_MS(start_frame));

  uint64_t duration_ns = frame->duration;
  if (duration_ns == GST_CLOCK_TIME_NONE) {
    warn("missing duration timestamp");
    // FIXME: fake duration if we don't get it
  }

  if (!have_time) {
    start_frame = last_frame_ + NS_TO_REFERENCE_TIME(duration_ns);
  }

  if (last_frame_ != 0 && last_frame_ >= start_frame) {
    warn("timestamp not monotonic now %lld nr: %llu last: %lld new: %lld",
      NS2MS(now_in_ns), frame->nr, REFERENCE_TIME_TO_MS(last_frame_), REFERENCE_TIME_TO_MS(start_frame));
    // fake it
    start_frame = last_frame_ + NS_TO_REFERENCE_TIME(duration_ns) / 2;
  }
  debug("timestamp log now %lld nr: %llu last: %lld new: %lld",
    NS2MS(now_in_ns), frame->nr, REFERENCE_TIME_TO_MS(last_frame_), REFERENCE_TIME_TO_MS(start_frame));

  end_frame = start_frame + NS_TO_REFERENCE_TIME(duration_ns);
  last_frame_ = start_frame;

  discontinuity = first_buffer || frame->discontinuity ? true : false;

  DxgiFrame* dxgi_frame = frame_pool_->GetFrame();
  if (dxgi_frame == NULL) {
    // TODO: drop frames, we're out of dxgi frames it is 1:1 w/ our shmem buffer
    // so it shouldn't really happen.
  }

  dxgi_frame->SetFrame(frame, i, start_frame, end_frame, discontinuity);
  *out_dxgi_frame = dxgi_frame; 

  uint64_t got_frame_diff_ms = last_got_frame_from_shmem_ms_ == 0 ? 0 : 
    GetCounterSinceStartMillisRounded(last_got_frame_from_shmem_ms_);

  debug("GOT frame from shmem. nr: %llu dxgi_handle: %llu pts: %lld dts: %lld i: %d read_ptr: %d write_ptr: %d behind: %d latency: %llu got_frame_diff: %llu",
      frame->nr,
      frame->dxgi_handle,
      frame->pts,
      frame->dts,
      i,
      shmem_->read_ptr,
      shmem_->write_ptr,
      shmem_->write_ptr - shmem_->read_ptr,
      frame->latency,
      got_frame_diff_ms);

  last_got_frame_from_shmem_ms_ = StartCounter();

  shmem_->read_ptr++;
  frame_total_cnt_ = shmem_->read_ptr - frame_start_;

  ReleaseMutex(shmem_mutex_);

  return S_OK;
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

  if (dxgi_frame->mapped_into_cpu && dxgi_frame->texture.Get()) {
    d3d_context_->Unmap(dxgi_frame->texture.Get(), 0);
  }

  dxgi_frame->mapped_into_cpu = false;

  if (WaitForSingleObject(shmem_mutex_, 1000) != WAIT_OBJECT_0) {
    error("didn't get lock");
    return S_FALSE;
  }

  auto shmFrame = GetShmFrame(dxgi_frame);

  // TODO: check it is the same frame!
  shmFrame->ref_cnt = shmFrame->ref_cnt - 2;

  debug("RELEASE shmem texture. nr: %lld dxgi_handle: %llu i: %d ref_cnt: %d",
      dxgi_frame->nr,
      dxgi_frame->dxgi_handle,
      dxgi_frame->index,
      shmFrame->ref_cnt);

  ReleaseMutex(shmem_mutex_);

  frame_pool_->ReturnFrame(dxgi_frame);
  return S_OK;
}

DxgiFrame::DxgiFrame() 
  : dxgi_handle(NULL), nr(0), index(0), 
    discontinuity(false),
    mapped_into_cpu(false),
    start_time(0L),
    end_time(0L),
    sent_to_gpu_time(0L) {
}

DxgiFrame::~DxgiFrame() {
  texture.Reset();
}

void DxgiFrame::SetFrame(struct frame *shmem_frame, uint64_t i,
    REFERENCE_TIME start_t, REFERENCE_TIME end_t, bool discont) {
  dxgi_handle = shmem_frame->dxgi_handle;
  nr = shmem_frame->nr;
  index = i;
  discontinuity = discont;
  mapped_into_cpu = false;
  frame_length = NS_TO_REFERENCE_TIME(shmem_frame->duration);
  start_time = start_t;
  end_time = end_t;
}

DxgiFramePool::DxgiFramePool(int size)
  : max_size_(size),
    created_size_(0) {
}

DxgiFramePool::~DxgiFramePool() {
  for (auto& frame : pool_) {
    delete frame;
  }
  pool_.clear();
}

DxgiFrame* DxgiFramePool::GetFrame() {
  if (pool_.empty()) {
    if (created_size_ > max_size_) {
      return NULL;
    }
    created_size_++;
    debug("CREATED new dxgi frame in the pool, created: %d, max limit: %d", created_size_, max_size_);
    return new DxgiFrame();
  } else {
    DxgiFrame* frame = pool_.front();
    pool_.pop_front();
    return frame;
  }
}

void DxgiFramePool::ReturnFrame(DxgiFrame* frame) {
  pool_.push_back(frame);
}
