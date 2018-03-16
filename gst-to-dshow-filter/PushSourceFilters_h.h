/* this ALWAYS GENERATED file contains the definitions for the interfaces */


/* File created by MIDL compiler version 8.00.0603 */
/* at Tue Feb 16 22:23:25 2016
 */
/* Compiler settings for PushSourceFilters.idl:
   Oicf, W1, Zp8, env=Win32 (32b run), target_arch=X86 8.00.0603 
protocol : dce , ms_ext, c_ext, robust
error checks: allocation ref bounds_check enum stub_data 
VC __declspec() decoration level: 
__declspec(uuid()), __declspec(selectany), __declspec(novtable)
DECLSPEC_UUID(), MIDL_INTERFACE()
 */
/* @@MIDL_FILE_HEADING(  ) */

#pragma warning( disable: 4049 )  /* more than 64k source lines */


/* verify that the <rpcndr.h> version is high enough to compile this file*/
#ifndef __REQUIRED_RPCNDR_H_VERSION__
#define __REQUIRED_RPCNDR_H_VERSION__ 475
#endif

#include "rpc.h"
#include "rpcndr.h"

#ifndef __RPCNDR_H_VERSION__
#error this stub requires an updated version of <rpcndr.h>
#endif // __RPCNDR_H_VERSION__


#ifndef __PushSourceFilters_h_h__
#define __PushSourceFilters_h_h__

#if defined(_MSC_VER) && (_MSC_VER >= 1020)
#pragma once
#endif

/* Forward Declarations */ 

#ifndef __CustomMemAllocator_FWD_DEFINED__
#define __CustomMemAllocator_FWD_DEFINED__

#ifdef __cplusplus
typedef class CustomMemAllocator CustomMemAllocator;
#else
typedef struct CustomMemAllocator CustomMemAllocator;
#endif /* __cplusplus */

#endif 	/* __CustomMemAllocator_FWD_DEFINED__ */


#ifndef __PushSource_FWD_DEFINED__
#define __PushSource_FWD_DEFINED__

#ifdef __cplusplus
typedef class PushSource PushSource;
#else
typedef struct PushSource PushSource;
#endif /* __cplusplus */

#endif 	/* __PushSource_FWD_DEFINED__ */


#ifndef __VideoSource_FWD_DEFINED__
#define __VideoSource_FWD_DEFINED__

#ifdef __cplusplus
typedef class VideoSource VideoSource;
#else
typedef struct VideoSource VideoSource;
#endif /* __cplusplus */

#endif 	/* __VideoSource_FWD_DEFINED__ */


#ifndef __AudioSource_FWD_DEFINED__
#define __AudioSource_FWD_DEFINED__

#ifdef __cplusplus
typedef class AudioSource AudioSource;
#else
typedef struct AudioSource AudioSource;
#endif /* __cplusplus */

#endif 	/* __AudioSource_FWD_DEFINED__ */


#ifndef __IPushSource_FWD_DEFINED__
#define __IPushSource_FWD_DEFINED__
typedef interface IPushSource IPushSource;

#endif 	/* __IPushSource_FWD_DEFINED__ */


#ifndef __IPushSource2_FWD_DEFINED__
#define __IPushSource2_FWD_DEFINED__
typedef interface IPushSource2 IPushSource2;

#endif 	/* __IPushSource2_FWD_DEFINED__ */


/* header files for imported files */
#include "unknwn.h"
#include "strmif.h"

#ifdef __cplusplus
extern "C"{
#endif 



#ifndef __PushSourceFiltersLib_LIBRARY_DEFINED__
#define __PushSourceFiltersLib_LIBRARY_DEFINED__

/* library PushSourceFiltersLib */
/* [helpstring][version][uuid] */ 


EXTERN_C const IID LIBID_PushSourceFiltersLib;

EXTERN_C const CLSID CLSID_CustomMemAllocator;

#ifdef __cplusplus

class DECLSPEC_UUID("e4a436cf-c222-4773-9483-27275d98e364")
CustomMemAllocator;
#endif

EXTERN_C const CLSID CLSID_PushSource;

#ifdef __cplusplus

class DECLSPEC_UUID("80afed4a-4fdb-49e7-910b-5ecf55f583b5")
PushSource;
#endif

EXTERN_C const CLSID CLSID_VideoSource;

#ifdef __cplusplus

class DECLSPEC_UUID("7ee57c6a-8493-4d74-a9ca-78717eebc05b")
VideoSource;
#endif

EXTERN_C const CLSID CLSID_AudioSource;

#ifdef __cplusplus

class DECLSPEC_UUID("50fa9528-e48f-4eca-9370-d9c3aa851968")
AudioSource;
#endif

#ifndef __IPushSource_INTERFACE_DEFINED__
#define __IPushSource_INTERFACE_DEFINED__

/* interface IPushSource */
/* [helpstring][uuid][object] */ 


// EXTERN_C const IID IID_IPushSource;
DEFINE_GUID(IID_IPushSource,
  0xe9ab2d2c, 0xcc81, 0x4ed6, 0x8f, 0x30, 0x1d, 0xe1, 0x14, 0xd2, 0x50, 0xaa);

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("7323aadc-8c6a-4eec-8461-e01691f14c88")
IPushSource : public IUnknown
{
  public:
    virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetFrameBuffer( 
        /* [out] */ unsigned char **ppBuffer,
        /* [out] */ unsigned long *pSize) = 0;

    virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Deliver( 
        /* [in] */ unsigned char *pBuffer) = 0;

};


#else 	/* C style interface */

typedef struct IPushSourceVtbl
{
  BEGIN_INTERFACE

    HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
        IPushSource * This,
        /* [in] */ REFIID riid,
        /* [annotation][iid_is][out] */ 
        _COM_Outptr_  void **ppvObject);

  ULONG ( STDMETHODCALLTYPE *AddRef )( 
      IPushSource * This);

  ULONG ( STDMETHODCALLTYPE *Release )( 
      IPushSource * This);

  /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetFrameBuffer )( 
      IPushSource * This,
      /* [out] */ unsigned char **ppBuffer,
      /* [out] */ unsigned long *pSize);

  /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Deliver )( 
      IPushSource * This,
      /* [in] */ unsigned char *pBuffer);

  END_INTERFACE
} IPushSourceVtbl;

interface IPushSource
{
  CONST_VTBL struct IPushSourceVtbl *lpVtbl;
};



#ifdef COBJMACROS


#define IPushSource_QueryInterface(This,riid,ppvObject)	\
  ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IPushSource_AddRef(This)	\
  ( (This)->lpVtbl -> AddRef(This) ) 

#define IPushSource_Release(This)	\
  ( (This)->lpVtbl -> Release(This) ) 


#define IPushSource_GetFrameBuffer(This,ppBuffer,pSize)	\
  ( (This)->lpVtbl -> GetFrameBuffer(This,ppBuffer,pSize) ) 

#define IPushSource_Deliver(This,pBuffer)	\
  ( (This)->lpVtbl -> Deliver(This,pBuffer) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IPushSource_INTERFACE_DEFINED__ */


#ifndef __IPushSource2_INTERFACE_DEFINED__
#define __IPushSource2_INTERFACE_DEFINED__

/* interface IPushSource2 */
/* [helpstring][uuid][object] */ 


EXTERN_C const IID IID_IPushSource2;

#if defined(__cplusplus) && !defined(CINTERFACE)

MIDL_INTERFACE("460aff21-5d91-4a58-84bf-582d9667ae25")
IPushSource2 : public IUnknown
{
  public:
    virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE GetFrameBuffer( 
        /* [out] */ IMediaSample **ppSample) = 0;

    virtual /* [helpstring] */ HRESULT STDMETHODCALLTYPE Deliver( 
        /* [in] */ IMediaSample *pSample) = 0;

};


#else 	/* C style interface */

typedef struct IPushSource2Vtbl
{
  BEGIN_INTERFACE

    HRESULT ( STDMETHODCALLTYPE *QueryInterface )( 
        IPushSource2 * This,
        /* [in] */ REFIID riid,
        /* [annotation][iid_is][out] */ 
        _COM_Outptr_  void **ppvObject);

  ULONG ( STDMETHODCALLTYPE *AddRef )( 
      IPushSource2 * This);

  ULONG ( STDMETHODCALLTYPE *Release )( 
      IPushSource2 * This);

  /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *GetFrameBuffer )( 
      IPushSource2 * This,
      /* [out] */ IMediaSample **ppSample);

  /* [helpstring] */ HRESULT ( STDMETHODCALLTYPE *Deliver )( 
      IPushSource2 * This,
      /* [in] */ IMediaSample *pSample);

  END_INTERFACE
} IPushSource2Vtbl;

interface IPushSource2
{
  CONST_VTBL struct IPushSource2Vtbl *lpVtbl;
};



#ifdef COBJMACROS


#define IPushSource2_QueryInterface(This,riid,ppvObject)	\
  ( (This)->lpVtbl -> QueryInterface(This,riid,ppvObject) ) 

#define IPushSource2_AddRef(This)	\
  ( (This)->lpVtbl -> AddRef(This) ) 

#define IPushSource2_Release(This)	\
  ( (This)->lpVtbl -> Release(This) ) 


#define IPushSource2_GetFrameBuffer(This,ppSample)	\
  ( (This)->lpVtbl -> GetFrameBuffer(This,ppSample) ) 

#define IPushSource2_Deliver(This,pSample)	\
  ( (This)->lpVtbl -> Deliver(This,pSample) ) 

#endif /* COBJMACROS */


#endif 	/* C style interface */




#endif 	/* __IPushSource2_INTERFACE_DEFINED__ */

#endif /* __PushSourceFiltersLib_LIBRARY_DEFINED__ */

/* Additional Prototypes for ALL interfaces */

/* end of Additional Prototypes */

#ifdef __cplusplus
}
#endif

#endif


