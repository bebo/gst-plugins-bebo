#pragma once

#include <windows.h>
#include <streams.h>

// -------------------------------------------------------------------------
class BeboAllocator : public CBaseAllocator
{

protected:
  LPBYTE m_pBuffer;   // combined memory for all buffers
                      // override to free the memory when decommit completes
                      // - we actually do nothing, and save the memory until deletion.
  void Free(void);

  // called from the destructor (and from Alloc if changing size/count) to
  // actually free up the memory
  void ReallyFree(void);

  // overriden to allocate the memory when commit called
  HRESULT Alloc(void);

  HRESULT GetBuffer(IMediaSample **ppBuffer,
                    REFERENCE_TIME *pStartTime,
                    REFERENCE_TIME *pEndTime,
                    DWORD dwFlags) override;


public:
  /* This goes in the factory template table to create new instances */
  static CUnknown *CreateInstance(__inout_opt LPUNKNOWN, __inout HRESULT *);

  STDMETHODIMP SetProperties(
    __in ALLOCATOR_PROPERTIES* pRequest,
    __out ALLOCATOR_PROPERTIES* pActual);

  BeboAllocator(__in_opt LPCTSTR, __inout_opt LPUNKNOWN, __inout HRESULT *);
#ifdef UNICODE
  BeboAllocator(__in_opt LPCSTR, __inout_opt LPUNKNOWN, __inout HRESULT *);
#endif
  ~BeboAllocator();
};
