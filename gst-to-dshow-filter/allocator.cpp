#include "allocator.h"
#include "logging.h"

CUnknown *BeboAllocator::CreateInstance(__inout_opt LPUNKNOWN pUnk, __inout HRESULT *phr) {
  CUnknown *pUnkRet = new BeboAllocator(NAME("BeboAllocator"), pUnk, phr);
  return pUnkRet;
}

BeboAllocator::BeboAllocator(
    __in_opt LPCTSTR pName,
    __inout_opt LPUNKNOWN pUnk,
    __inout HRESULT *phr)
  : CBaseAllocator(pName, pUnk, phr, TRUE, TRUE),
  m_pBuffer(NULL) {
  }

#ifdef UNICODE
BeboAllocator::BeboAllocator(
    __in_opt LPCSTR pName,
    __inout_opt LPUNKNOWN pUnk,
    __inout HRESULT *phr)
  : CBaseAllocator(pName, pUnk, phr, TRUE, TRUE),
  m_pBuffer(NULL) {
  }
#endif

/* Destructor frees our memory resources */

BeboAllocator::~BeboAllocator() {
  Decommit();
  ReallyFree();
}

/* This sets the size and count of the required samples. The memory isn't
   actually allocated until Commit() is called, if memory has already been
   allocated then assuming no samples are outstanding the user may call us
   to change the buffering, the memory will be released in Commit() */
STDMETHODIMP
BeboAllocator::SetProperties(
    __in ALLOCATOR_PROPERTIES* pRequest,
    __out ALLOCATOR_PROPERTIES* pActual) {
  CheckPointer(pActual, E_POINTER);
  ValidateReadWritePtr(pActual, sizeof(ALLOCATOR_PROPERTIES));
  CAutoLock cObjectLock(this);

  ZeroMemory(pActual, sizeof(ALLOCATOR_PROPERTIES));

  ASSERT(pRequest->cbBuffer > 0);

  SYSTEM_INFO SysInfo;
  GetSystemInfo(&SysInfo);

  /*  Check the alignment request is a power of 2 */
  if ((-pRequest->cbAlign & pRequest->cbAlign) != pRequest->cbAlign) {
    DbgLog((LOG_ERROR, 1, TEXT("Alignment requested 0x%x not a power of 2!"),
          pRequest->cbAlign));
  }
  /*  Check the alignment requested */
  if (pRequest->cbAlign == 0 ||
      (SysInfo.dwAllocationGranularity & (pRequest->cbAlign - 1)) != 0) {
    DbgLog((LOG_ERROR, 1, TEXT("Invalid alignment 0x%x requested - granularity = 0x%x"),
          pRequest->cbAlign, SysInfo.dwAllocationGranularity));
    return VFW_E_BADALIGN;
  }

  /* Can't do this if already committed, there is an argument that says we
     should not reject the SetProperties call if there are buffers still
     active. However this is called by the source filter, which is the same
     person who is holding the samples. Therefore it is not unreasonable
     for them to free all their samples before changing the requirements */

  if (m_bCommitted == TRUE) {
    return VFW_E_ALREADY_COMMITTED;
  }

  /* Must be no outstanding buffers */

  if (m_lFree.GetCount() < m_lAllocated) {
    return VFW_E_BUFFERS_OUTSTANDING;
  }

  /* There isn't any real need to check the parameters as they
     will just be rejected when the user finally calls Commit */

  // round length up to alignment - remember that prefix is included in
  // the alignment
  LONG lSize = pRequest->cbBuffer + pRequest->cbPrefix;
  LONG lRemainder = lSize % pRequest->cbAlign;
  if (lRemainder != 0) {
    lSize = lSize - lRemainder + pRequest->cbAlign;
  }
  pActual->cbBuffer = m_lSize = (lSize - pRequest->cbPrefix);

  pActual->cBuffers = m_lCount = pRequest->cBuffers;
  pActual->cbAlign = m_lAlignment = pRequest->cbAlign;
  pActual->cbPrefix = m_lPrefix = pRequest->cbPrefix;

  m_bChanged = TRUE;
  return NOERROR;
}

// override this to allocate our resources when Commit is called.
//
// note that our resources may be already allocated when this is called,
// since we don't free them on Decommit. We will only be called when in
// decommit state with all buffers free.
//
// object locked by caller
HRESULT
BeboAllocator::Alloc(void) {
  info("JAKE alloc");
  CAutoLock lck(this);

  /* Check he has called SetProperties */
  HRESULT hr = CBaseAllocator::Alloc();
  if (FAILED(hr)) {
    return hr;
  }

  /* If the requirements haven't changed then don't reallocate */
  if (hr == S_FALSE) {
    ASSERT(m_pBuffer);
    return NOERROR;
  }
  ASSERT(hr == S_OK); // we use this fact in the loop below

  /* Free the old resources */
  if (m_pBuffer) {
    ReallyFree();
  }

  /* Make sure we've got reasonable values */
  if (m_lSize < 0 || m_lPrefix < 0 || m_lCount < 0) {
    return E_OUTOFMEMORY;
  }

  /* Compute the aligned size */
  LONG lAlignedSize = m_lSize + m_lPrefix;

  /*  Check overflow */
  if (lAlignedSize < m_lSize) {
    return E_OUTOFMEMORY;
  }

  if (m_lAlignment > 1) {
    LONG lRemainder = lAlignedSize % m_lAlignment;
    if (lRemainder != 0) {
      LONG lNewSize = lAlignedSize + m_lAlignment - lRemainder;
      if (lNewSize < lAlignedSize) {
        return E_OUTOFMEMORY;
      }
      lAlignedSize = lNewSize;
    }
  }

  /* Create the contiguous memory block for the samples
     making sure it's properly aligned (64K should be enough!)
     */
  ASSERT(lAlignedSize % m_lAlignment == 0);

  LONGLONG lToAllocate = m_lCount * (LONGLONG)lAlignedSize;

  /*  Check overflow */
  if (lToAllocate > MAXLONG) {
    return E_OUTOFMEMORY;
  }

  m_pBuffer = (PBYTE)VirtualAlloc(NULL,
      (LONG)lToAllocate,
      MEM_COMMIT,
      PAGE_READWRITE);

  if (m_pBuffer == NULL) {
    return E_OUTOFMEMORY;
  }

  LPBYTE pNext = m_pBuffer;
  CMediaSample *pSample;

  ASSERT(m_lAllocated == 0);

  // Create the new samples - we have allocated m_lSize bytes for each sample
  // plus m_lPrefix bytes per sample as a prefix. We set the pointer to
  // the memory after the prefix - so that GetPointer() will return a pointer
  // to m_lSize bytes.
  for (; m_lAllocated < m_lCount; m_lAllocated++, pNext += lAlignedSize) {


    pSample = new CMediaSample(
        NAME("Default memory media sample"),
        this,
        &hr,
        pNext + m_lPrefix,      // GetPointer() value
        m_lSize);               // not including prefix

    ASSERT(SUCCEEDED(hr));
    if (pSample == NULL) {
      return E_OUTOFMEMORY;
    }

    // This CANNOT fail
    m_lFree.Add(pSample);
  }

  m_bChanged = FALSE;
  return NOERROR;
}


// override this to free up any resources we have allocated.
// called from the base class on Decommit when all buffers have been
// returned to the free list.
//
// caller has already locked the object.

// in our case, we keep the memory until we are deleted, so
// we do nothing here. The memory is deleted in the destructor by
// calling ReallyFree()
void
BeboAllocator::Free(void) {
  info("JAKE free");
  return;
}


// called from the destructor (and from Alloc if changing size/count) to
// actually free up the memory
void
BeboAllocator::ReallyFree(void) {
  info("JAKE REALLY FREE");
  /* Should never be deleting this unless all buffers are freed */

  ASSERT(m_lAllocated == m_lFree.GetCount());

  /* Free up all the CMediaSamples */

  CMediaSample *pSample;
  for (;;) {
    pSample = m_lFree.RemoveHead();
    if (pSample != NULL) {
      delete pSample;
    }
    else {
      break;
    }
  }

  m_lAllocated = 0;

  // free the block of buffer memory
  if (m_pBuffer) {
    EXECUTE_ASSERT(VirtualFree(m_pBuffer, 0, MEM_RELEASE));
    m_pBuffer = NULL;
  }
}

HRESULT BeboAllocator::GetBuffer(IMediaSample **ppBuffer,
    REFERENCE_TIME *pStartTime,
    REFERENCE_TIME *pEndTime,
    DWORD dwFlags) {
  info("JAKE GETBUFFER");
  UNREFERENCED_PARAMETER(pStartTime);
  UNREFERENCED_PARAMETER(pEndTime);
  UNREFERENCED_PARAMETER(dwFlags);
  CMediaSample *pSample;

  *ppBuffer = NULL;
  for (;;)
  {
    {  // scope for lock
      CAutoLock cObjectLock(this);

      /* Check we are committed */
      if (!m_bCommitted) {
        return VFW_E_NOT_COMMITTED;
      }
      pSample = (CMediaSample *) m_lFree.RemoveHead();
      if (pSample == NULL) {
        SetWaiting();
      }
    }

    /* If we didn't get a sample then wait for the list to signal */

    if (pSample) {
      break;
    }
    if (dwFlags & AM_GBF_NOWAIT) {
      return VFW_E_TIMEOUT;
    }
    ASSERT(m_hSem != NULL);
    WaitForSingleObject(m_hSem, INFINITE);
  }

  /* Addref the buffer up to one. On release
     back to zero instead of being deleted, it will requeue itself by
     calling the ReleaseBuffer member function. NOTE the owner of a
     media sample must always be derived from CBaseAllocator */


  ASSERT(pSample->m_cRef == 0);
  pSample->m_cRef = 1;
  *ppBuffer = pSample;

#ifdef DXMPERF
  PERFLOG_GETBUFFER( (IMemAllocator *) this, pSample );
#endif // DXMPERF

  return NOERROR;
}



