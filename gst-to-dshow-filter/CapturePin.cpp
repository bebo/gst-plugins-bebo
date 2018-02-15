#include <streams.h>

#include <tchar.h>
#include "Capture.h"
#include "CaptureGuids.h"
#include "DibHelper.h"
#include <wmsdkidl.h>
#include "Logging.h"
#include "CommonTypes.h"
#include "d3d11.h"
#include <dxgi.h>
#include <Psapi.h>
#include "libyuv/convert.h"

#ifndef MIN
#define MIN(a,b)  ((a) < (b) ? (a) : (b))  // danger! can evaluate "a" twice.
#endif

#define EVENT_READ_REGISTRY "Global\\BEBO_CAPTURE_READ_REGISTRY"

extern "C" {
	extern bool load_graphics_offsets(bool is32bit);
}

DWORD globalStart; // for some debug performance benchmarking
uint64_t countMissed = 0;
long double fastestRoundMillis = 1000000; // random big number
long double sumMillisTook = 0;

wchar_t out[1024];
// FIXME :  move these
bool ever_started = false;
boolean missed = false;

#ifdef _DEBUG 
int show_performance = 1;
#else
int show_performance = 0;
#endif

volatile bool initialized = false;

// the default child constructor...
CPushPinDesktop::CPushPinDesktop(HRESULT *phr, CGameCapture *pFilter)
	: CSourceStream(NAME("Push Source CPushPinDesktop child/pin"), phr, pFilter, L"Capture"),
	m_iFrameNumber(0),
	m_pParent(pFilter),
	m_bFormatAlreadySet(false),
	previousFrame(0),
	active(false),
	m_iCaptureType(-1),
	m_pCaptureTypeName(L""),
	m_pCaptureLabel(L""),
	m_pCaptureId(L""),
	m_pCaptureWindowName(NULL),
	m_pCaptureWindowClassName(NULL),
	m_pCaptureExeFullName(NULL),
	m_iDesktopNumber(-1),
	m_iDesktopAdapterNumber(-1),
	m_iCaptureHandle(-1),
	m_bCaptureOnce(0),
	m_iCaptureConfigWidth(0),
	m_iCaptureConfigHeight(0),
	m_rtFrameLength(UNITS / 30),
	readRegistryEvent(NULL),
	init_hooks_thread(NULL),
	threadCreated(false),
	isBlackFrame(true),
	blackFrameCount(0),
    shmem_(nullptr)
{
	info("CPushPinDesktop");

	registry.Open(HKEY_CURRENT_USER, L"Software\\Bebo\\GstCapture", KEY_READ);

	if (!readRegistryEvent) {
		readRegistryEvent = CreateEvent(NULL,
			TRUE,
			FALSE,
			TEXT(EVENT_READ_REGISTRY));

		if (readRegistryEvent == NULL) {
			warn("Failed to create read registry signal event. Attempting to open event.");
			readRegistryEvent = OpenEvent(EVENT_ALL_ACCESS,
				FALSE,
				TEXT(EVENT_READ_REGISTRY));

			if (readRegistryEvent == NULL) {
				error("Failed to open registry signal event, after attempted to create it. We should die here.");
			}
		} else {
			info("Created read registry signal event. Handle: %llu", readRegistryEvent);
		}
	}

	// now read some custom settings...
	WarmupCounter();

	GetGameFromRegistry();
}

CPushPinDesktop::~CPushPinDesktop()
{
    // FIXME release resources...

	if (readRegistryEvent) {
		CloseHandle(readRegistryEvent);
	}

	CleanupCapture();
}

void CPushPinDesktop::CleanupCapture() {

	if (!threadCreated) {
		LOG(INFO) << "Total no. Frames written: " << m_iFrameNumber << ", before thread created.";
	} else {
		char out_c[1024] = { 0 };
		WideCharToMultiByte(CP_UTF8, 0, out, 1024, out_c, 1024, NULL, NULL);
		LOG(INFO) << "Total no. Frames written: " << m_iFrameNumber << " " << out_c;
	}

	// reset counter values 

	globalStart = GetTickCount();
	missed = true;
	countMissed = 0;
	sumMillisTook = 0;
	fastestRoundMillis = LONG_MAX;
	m_iFrameNumber = 0;
	previousFrame = 0;
	isBlackFrame = true;
	blackFrameCount = 0;

	_swprintf(out, L"done video frame! total frames: %I64d this one %dx%d -> (%dx%d) took: %.02Lfms, %.02f ave fps (%.02f is the theoretical max fps based on this round, ave. possible fps %.02f, fastest round fps %.02f, negotiated fps %.06f), frame missed: %I64d, type: %ls, name: %ls, black frame: %d, black frame count: %llu",
		m_iFrameNumber, m_iCaptureConfigWidth, m_iCaptureConfigHeight, getNegotiatedFinalWidth(), getNegotiatedFinalHeight(), 
		0.0, 0.0, 0.0, 0.0, 0.0, 0.0, countMissed, 
		m_pCaptureTypeName.c_str(), m_pCaptureLabel.c_str(), isBlackFrame, blackFrameCount);
}

int CPushPinDesktop::GetGameFromRegistry(void) {
	std::wstringstream message;
	message << "Reading from registry: ";

	int numberOfChanges = 0;

	if (registry.HasValue(TEXT("CaptureType"))) {
		std::wstring data;
		registry.ReadValue(TEXT("CaptureType"), &data);

		int newCaptureType = -1;
		wchar_t type[1024];
		_swprintf(type, L"%ls", data.c_str());

		if (wcscmp(type, L"desktop") == 0) {
			newCaptureType = CAPTURE_DESKTOP;
		}
		else if (wcscmp(type, L"inject") == 0) {
			newCaptureType = CAPTURE_INJECT;
		}
		else if (wcscmp(type, L"gdi") == 0) {
			newCaptureType = CAPTURE_GDI;
		}
		else if (wcscmp(type, L"dshow") == 0) {
			newCaptureType = CAPTURE_DSHOW;
		}
		if (newCaptureType > -1 && m_iCaptureType != newCaptureType) {
			m_iCaptureType = newCaptureType;
			m_pCaptureTypeName = data;
			message << "CaptureType: " << type << " (" << m_iCaptureType << "), ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("CaptureId"))) {
		std::wstring data;
		registry.ReadValue(TEXT("CaptureId"), &data);

		wchar_t text[1024];
		_swprintf(text, L"%ls", data.c_str());

		int oldAdapterId = m_iDesktopAdapterNumber;
		int oldDesktopId = m_iDesktopNumber;
		int newAdapterId = m_iDesktopAdapterNumber;
		int newDesktopId = m_iDesktopNumber;

		wchar_t * typeName = _wcstok(text, L":");
		wchar_t * adapterId = _wcstok(NULL, L":");
		wchar_t * desktopId = _wcstok(NULL, L":");

		if (adapterId != NULL) {
			if (wcscmp(typeName, L"desktop") == 0) {
				newAdapterId = _wtoi(adapterId);
			}
		}

		if (desktopId != NULL) {
			if (wcscmp(typeName, L"desktop") == 0) {
				newDesktopId = _wtoi(desktopId);
			}
		}

		if (oldAdapterId != newAdapterId ||
			oldDesktopId != newDesktopId) {
			m_iDesktopAdapterNumber = newAdapterId;
			m_iDesktopNumber = newDesktopId;
			m_pCaptureId = data;
			message << "CaptureId: " << typeName << ":" << m_iDesktopAdapterNumber << ":" << m_iDesktopNumber << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("CaptureWindowName"))) {
		std::wstring data;

		registry.ReadValue(TEXT("CaptureWindowName"), &data);

		if (m_pCaptureWindowName == NULL || wcscmp(data.c_str(), m_pCaptureWindowName) != 0) {
			if (m_pCaptureWindowName != NULL) {
				delete[] m_pCaptureWindowName;
			}
			m_pCaptureWindowName = _wcsdup(data.c_str());
			message << "CaptureWindowName: " << m_pCaptureWindowName << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("CaptureWindowClassName"))) {
		std::wstring data;

		registry.ReadValue(TEXT("CaptureWindowClassName"), &data);

		if (m_pCaptureWindowClassName == NULL || wcscmp(data.c_str(), m_pCaptureWindowClassName) != 0) {
			if (m_pCaptureWindowClassName != NULL) {
				delete[] m_pCaptureWindowClassName;
			}
			m_pCaptureWindowClassName = _wcsdup(data.c_str());
			message << "CaptureWindowClassName: " << m_pCaptureWindowClassName << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("CaptureExeFullName"))) {
		std::wstring data;

		registry.ReadValue(TEXT("CaptureExeFullName"), &data);

		if (m_pCaptureExeFullName == NULL || wcscmp(data.c_str(), m_pCaptureExeFullName) != 0) {
			if (m_pCaptureExeFullName != NULL) {
				delete[] m_pCaptureExeFullName;
			}
			m_pCaptureExeFullName = _wcsdup(data.c_str());
			message << "CaptureExeFullName: " << m_pCaptureExeFullName << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("CaptureLabel"))) {
		std::wstring data;

		registry.ReadValue(TEXT("CaptureLabel"), &data);

		if (m_iCaptureType == CAPTURE_DESKTOP && data.length() == 0) {
			if (m_pCaptureLabel.compare(m_pCaptureId) != 0) {
				m_pCaptureLabel = m_pCaptureId;
				message << "CaptureLabel: " << m_pCaptureLabel << ", ";
				numberOfChanges++;
			}
		} else if (data.compare(m_pCaptureLabel) != 0) {
			m_pCaptureLabel = data;
			message << "CaptureLabel: " << m_pCaptureLabel << ", ";
			numberOfChanges++;
		}
	}

	if (registry.HasValue(TEXT("CaptureWindowHandle"))) {
		int64_t qout;

		registry.ReadInt64(TEXT("CaptureWindowHandle"), &qout);

		if (m_iCaptureHandle != qout) {
			m_iCaptureHandle = qout;
			message << "CaptureWindowHandle: " << m_iCaptureHandle << ", ";
			numberOfChanges++;
		}
	}



	if (numberOfChanges > 0) {
		std::wstring wstr = message.str();
		wstr.erase(wstr.size() - 2);
		info("%ls", wstr.c_str());
	}

	return numberOfChanges;
}


HRESULT CPushPinDesktop::Inactive(void) {
	active = false;
	return CSourceStream::Inactive();
};

HRESULT CPushPinDesktop::Active(void) {
	active = true;
	return CSourceStream::Active();
};

void CPushPinDesktop::ProcessRegistryReadEvent(long timeout) {
	DWORD result = WaitForSingleObject(readRegistryEvent, timeout);
	if (result == WAIT_OBJECT_0) {
		int changeCount = GetGameFromRegistry();

		if (changeCount > 0) {
		    info("Received re-read registry event, number of changes in registry: %d", changeCount);
			CleanupCapture();
		}

		ResetEvent(readRegistryEvent);
	}
}

HRESULT CPushPinDesktop::FillBuffer(IMediaSample *pSample)
{
	__int64 startThisRound = StartCounter();

	CheckPointer(pSample, E_POINTER);

	uint64_t millisThisRoundTook = 0;
	CRefTime now;
	now = 0;

	boolean gotFrame = false;
	while (!gotFrame) {
		if (!active) {
			info("FillBuffer - inactive");
			return S_FALSE;
		}

		ProcessRegistryReadEvent(0);

		int code = FillBuffer_GST(pSample);

		// failed to initialize desktop capture, sleep is to reduce the # of log that we failed
		// this failure can happen pretty often due to mobile graphic card
		// we should detect + log this smartly instead of putting 3s sleep hack.

		if (code == S_OK) {
			gotFrame = true;
		} else if (code == 2) { // not initialized yet
			gotFrame = false;
			//long sleep = (m_iCaptureType == CAPTURE_DESKTOP) ? 3000 : 300;
			ProcessRegistryReadEvent(0);
			continue;
		} else if (code == 3) { // black frame
			gotFrame = false;

			long double avg_fps = (sumMillisTook == 0) ? 0 : 1.0 * 1000 * m_iFrameNumber / sumMillisTook;
			long double max = (millisThisRoundTook == 0) ? 0 : 1.0 * 1000 / millisThisRoundTook;

			double m_fFpsSinceBeginningOfTime = ((double)m_iFrameNumber) / (GetTickCount() - globalStart) * 1000;

			_swprintf(out, L"done video frame! total frames: %d this one %dx%d -> (%dx%d) took: %.02Lfms, %.02f ave fps (%.02f is the theoretical max fps based on this round, ave. possible fps %.02f, fastest round fps %.02f, negotiated fps %.06f), frame missed: %d, type: %ls, name: %ls, black frame: %d, black frame count: %llu",
				m_iFrameNumber, m_iCaptureConfigWidth, m_iCaptureConfigHeight, 
				getNegotiatedFinalWidth(), getNegotiatedFinalHeight(), millisThisRoundTook, m_fFpsSinceBeginningOfTime, 
				max, sumMillisTook, 
				1.0 * 1000 / fastestRoundMillis, GetFps(), countMissed, m_pCaptureTypeName.c_str(), m_pCaptureLabel.c_str(), isBlackFrame, blackFrameCount);
		} else {
			gotFrame = false;
			ProcessRegistryReadEvent(5000);
		}
	}

	missed = false;
	millisThisRoundTook = GetCounterSinceStartMillis(startThisRound);
	fastestRoundMillis = min(millisThisRoundTook, fastestRoundMillis);
	sumMillisTook += millisThisRoundTook;

	// accomodate for 0 to avoid startup negatives, which would kill our math on the next loop...
	previousFrame = max(0, previousFrame);
	// auto-correct drift
	previousFrame = previousFrame + m_rtFrameLength;

	REFERENCE_TIME startFrame = m_iFrameNumber * m_rtFrameLength;
	REFERENCE_TIME endFrame = startFrame + m_rtFrameLength;
	pSample->SetTime((REFERENCE_TIME *)&startFrame, (REFERENCE_TIME *)&endFrame);
	CSourceStream::m_pFilter->StreamTime(now);
	debug("timestamping (%11f) video packet %llf -> %llf length:(%11f) drift:(%llf)", 0.0001 * now, 0.0001 * startFrame, 0.0001 * endFrame, 0.0001 * (endFrame - startFrame), 0.0001 * (now - previousFrame));

	m_iFrameNumber++;

	if ((m_iFrameNumber - countMissed) == 1) {
		info("Got first frame, type: %ls, name: %ls", m_pCaptureTypeName.c_str(), m_pCaptureLabel.c_str());
	}

	// Set TRUE on every sample for uncompressed frames http://msdn.microsoft.com/en-us/library/windows/desktop/dd407021%28v=vs.85%29.aspx
	pSample->SetSyncPoint(TRUE);

	// only set discontinuous for the first...I think...
	pSample->SetDiscontinuity(m_iFrameNumber <= 1);

	double m_fFpsSinceBeginningOfTime = ((double)m_iFrameNumber) / (GetTickCount() - globalStart) * 1000;
	_swprintf(out, L"done video frame! total frames: %d this one %dx%d -> (%dx%d) took: %.02Lfms, %.02f ave fps (%.02f is the theoretical max fps based on this round, ave. possible fps %.02f, fastest round fps %.02f, negotiated fps %.06f), frame missed: %d, type: %ls, name: %ls, black frame: %d, black frame count: %llu",
		m_iFrameNumber, m_iCaptureConfigWidth, m_iCaptureConfigHeight, getNegotiatedFinalWidth(), getNegotiatedFinalHeight(), millisThisRoundTook, m_fFpsSinceBeginningOfTime, 1.0 * 1000 / millisThisRoundTook,
		/* average */ 1.0 * 1000 * m_iFrameNumber / sumMillisTook, 1.0 * 1000 / fastestRoundMillis, GetFps(), countMissed, m_pCaptureTypeName.c_str(), m_pCaptureLabel.c_str(), isBlackFrame, blackFrameCount);
//FIXM	debug(out);
	return S_OK;
}


HRESULT CPushPinDesktop::OpenShmMem()
{
    if (shmem_ != nullptr) {
       return S_OK;
    }

    shmem_new_data_semaphore_ = OpenSemaphore(SYNCHRONIZE, false, BEBO_SHMEM_DATA_SEM);
    if (!shmem_new_data_semaphore_) {
        error("could not open semaphore mapping %d", GetLastError());
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
    ReleaseMutex(shmem_mutex_);
    if (!shmem_) {
        error("could not map shmem %d", GetLastError());
        return -1;
    }
    info("Opened Shared Memory Buffer");
    return S_OK;
}


HRESULT CPushPinDesktop::FillBuffer_GST(IMediaSample *pSample)
{
	CheckPointer(pSample, E_POINTER);
    if (OpenShmMem() != S_OK) {
        return 2;
    }

    if (!shmem_) {
        return 2;
    }

    if (WaitForSingleObject(shmem_mutex_, INFINITE) == WAIT_OBJECT_0) {
        info("GOT MUTEX");
    }

    while (shmem_->write_ptr == 0 || shmem_->read_ptr >= shmem_->write_ptr) {

        info("waiting - no data read_ptr: %d write_ptr: %d",
            shmem_->read_ptr,
            shmem_->write_ptr);
        DWORD result = SignalObjectAndWait(shmem_mutex_,
                                           shmem_new_data_semaphore_,
                                           200,
                                           false);
        if (result == WAIT_TIMEOUT) {
            debug("timed out after 200ms");
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
            info("waiting - done");
            if (WaitForSingleObject(shmem_mutex_, INFINITE) == WAIT_OBJECT_0) {
                info("GOT MUTEX");
                continue;
            }
        }
    }

    // FIXME - handle late start case
    // FIXME - handle delay case

//    ReleaseMutex(shmem_mutex);

	CRefTime now;
	now = 0;

	CSourceStream::m_pFilter->StreamTime(now);

//	if (now <= 0) {
//		DWORD dwMilliseconds = (DWORD)(m_rtFrameLength / 10000L);
//		debug("no reference graph clock - sleeping %d", dwMilliseconds);
//		Sleep(dwMilliseconds);
//	}
//	else if (now < (previousFrame + m_rtFrameLength)) {
//		DWORD dwMilliseconds = (DWORD)max(1, min((previousFrame + m_rtFrameLength - now), m_rtFrameLength) / 10000L);
//		debug("sleeping - %d", dwMilliseconds);
//		Sleep(dwMilliseconds);
//	}
//	else if (missed) {
//		DWORD dwMilliseconds = (DWORD)(m_rtFrameLength / 20000L);
//		debug("starting/missed - sleeping %d", dwMilliseconds);
//		Sleep(dwMilliseconds);
//		CSourceStream::m_pFilter->StreamTime(now);
//	}
//	else if (now > (previousFrame + 2 * m_rtFrameLength)) {
//		uint64_t missed_nr = (now - m_rtFrameLength - previousFrame) / m_rtFrameLength;
//		m_iFrameNumber += missed_nr;
//		countMissed += missed_nr;
//		debug("missed %d frames can't keep up %d %d %.02f %llf %llf %11f",
//			missed_nr, m_iFrameNumber, countMissed, (100.0L*countMissed / m_iFrameNumber), 0.0001 * now, 0.0001 * previousFrame, 0.0001 * (now - m_rtFrameLength - previousFrame));
//		previousFrame = previousFrame + missed_nr * m_rtFrameLength;
//		missed = true;
//	}

    uint64_t i = shmem_->read_ptr % shmem_->count;
    shmem_->read_ptr++;

    uint64_t frame_offset = shmem_->frame_offset + i * shmem_->frame_size;
    uint64_t data_offset = shmem_->frame_data_offset;
    struct frame_header *frame_header = (struct frame_header*) (((char*)shmem_) + frame_offset);
    BYTE *data = ((BYTE *)frame_header) + data_offset;

    uint64_t sample_size = pSample->GetSize();
    info("pts: %lld i: %d frame_offset: %d offset: data_offset: %d size: %d read_ptr: %d write_ptr: %d behind: %d",
       // frame_header->dts,
        frame_header->pts,
        i,
        frame_offset,
        data_offset,
        sample_size,
        shmem_->read_ptr,
        shmem_->write_ptr,
        shmem_->write_ptr - shmem_->read_ptr);

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

    ReleaseMutex(shmem_mutex_);
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
	pProperties->cbBuffer = header.biHeight * header.biWidth * 3 / 2; // necessary to prevent an "out of memory" error for FMLE. Yikes. Oh wow yikes.

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

	previousFrame = 0; // reset
	m_iFrameNumber = 0;

	return NOERROR;
} // DecideBufferSize


HRESULT CPushPinDesktop::OnThreadCreate() {
	info("CPushPinDesktop OnThreadCreate");
	previousFrame = 0; // reset <sigh> dunno if this helps FME which sometimes had inconsistencies, or not
	m_iFrameNumber = 0;
	threadCreated = true;
	return S_OK;
}

HRESULT CPushPinDesktop::OnThreadDestroy() {
	info("CPushPinDesktop::OnThreadDestroy");
	CleanupCapture();
	return NOERROR;
};

HRESULT CPushPinDesktop::OnThreadStartPlay() {
	debug("CPushPinDesktop::OnThreadStartPlay()");
	return NOERROR;
};

BOOL CALLBACK WindowsProcVerifier(HWND hwnd, LPARAM param)
{
	EnumWindowParams* p = reinterpret_cast<EnumWindowParams*>(param);
	bool hwnd_match = (QWORD) hwnd == p->find_hwnd;
	bool hwnd_must_match = p->find_hwnd_must_match; // capture type specify instance only TODO

	if (!hwnd_match && hwnd_must_match) { 
		return TRUE;
	}

	if (!IsWindowVisible(hwnd)) {
		return TRUE;
	}

	HWND hwnd_try = GetAncestor(hwnd, GA_ROOTOWNER);
	HWND hwnd_walk = NULL;
	while (hwnd_try != hwnd_walk) {
		hwnd_walk = hwnd_try;
		hwnd_try = GetLastActivePopup(hwnd_walk);
		if (IsWindowVisible(hwnd_try))
			break;
	}

	if (hwnd_walk != hwnd) {
		return TRUE;
	}

	TITLEBARINFO ti;
	// the following removes some task tray programs and "Program Manager"
	ti.cbSize = sizeof(ti);
	GetTitleBarInfo(hwnd, &ti);
	if (ti.rgstate[0] & STATE_SYSTEM_INVISIBLE && !(ti.rgstate[0] & STATE_SYSTEM_FOCUSABLE)) {
		return TRUE;
	}

	// Tool windows should not be displayed either, these do not appear in the
	// task bar.
	if (GetWindowLong(hwnd, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) {
		return TRUE;
	}

	const int buf_len = 1024;

	TCHAR class_name[buf_len] = { 0 };
	GetClassName(hwnd, class_name, buf_len);

	// check if class match
	bool class_match = lstrcmp(class_name, p->find_class_name) == 0;

	// check exe match
	bool exe_match = false;

	if (p->find_exe_name && wcslen(p->find_exe_name) > 0) {
		DWORD pid;
		GetWindowThreadProcessId(hwnd, &pid);
		HANDLE handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, pid);
		TCHAR exe_name[buf_len] = { 0 };

		if (handle != NULL) {
			GetModuleFileNameEx(handle, NULL, exe_name, buf_len);
			exe_match = lstrcmp(exe_name, p->find_exe_name) == 0;
			CloseHandle(handle);
		}
	} else {
		exe_match = true;
	}

	bool found = exe_match && class_match;
	if (found) {
		p->to_capture_hwnd = hwnd;
		p->to_window_found = true;
	}

	return !found;
}

HWND CPushPinDesktop::FindCaptureWindows(bool hwnd_must_match, QWORD capture_handle, LPWSTR capture_class, LPWSTR capture_name, LPWSTR capture_exe_name) {
	EnumWindowParams cb;
	cb.find_hwnd = capture_handle;
	cb.find_class_name = capture_class;
	cb.find_window_name = capture_name;
	cb.find_exe_name = capture_exe_name;
	cb.find_hwnd_must_match = hwnd_must_match;
	cb.to_window_found = false;
	cb.to_capture_hwnd = NULL;

	EnumWindows(&WindowsProcVerifier, reinterpret_cast<LPARAM>(&cb));
	return cb.to_capture_hwnd;
}
