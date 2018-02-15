//------------------------------------------------------------------------------
// File: Setup.cpp
//
// Desc: DirectShow sample code - implementation of PushSource sample filters
//
// Copyright (c)  Microsoft Corporation.  All rights reserved.
//------------------------------------------------------------------------------

#include <Windows.h>
#include <streams.h>
#include <initguid.h>
#include "Logging.h"
#include "names_and_ids.h"
#include "Capture.h"
#include "DibHelper.h"

HMODULE g_hModule = NULL;

// Note: It is better to register no media types than to register a partial 
// media type (subtype == GUID_NULL) because that can slow down intelligent connect 
// for everyone else.

// For a specialized source filter like this, it is best to leave out the 
// AMOVIESETUP_FILTER altogether, so that the filter is not available for 
// intelligent connect. Instead, use the CLSID to create the filter or just 
// use 'new' in your application.


// Filter setup data
const AMOVIESETUP_MEDIATYPE sudOpPinTypes =
{
    &MEDIATYPE_Video,       // Major type
    &MEDIASUBTYPE_NULL      // Minor type
};


const AMOVIESETUP_PIN sudOutputPinBitmap = 
{
    L"Output",      // Obsolete, not used.
    FALSE,          // Is this pin rendered?
    TRUE,           // Is it an output pin?
    FALSE,          // Can the filter create zero instances?
    FALSE,          // Does the filter create multiple instances?
    &CLSID_NULL,    // Obsolete.
    NULL,           // Obsolete.
    1,              // Number of media types.
    &sudOpPinTypes  // Pointer to media types.
};

const AMOVIESETUP_PIN sudOutputPinDesktop = 
{
    L"Output",      // Obsolete, not used.
    FALSE,          // Is this pin rendered?
    TRUE,           // Is it an output pin?
    FALSE,          // Can the filter create zero instances?
    FALSE,          // Does the filter create multiple instances?
    &CLSID_NULL,    // Obsolete.
    NULL,           // Obsolete.
    1,              // Number of media types.
    &sudOpPinTypes  // Pointer to media types.
};

const AMOVIESETUP_FILTER sudPushSourceDesktop =
{
    &CLSID_PushSourceDesktop,// Filter CLSID
    DS_FILTER_DESCRIPTION,       // String name
    MERIT_DO_NOT_USE,       // Filter merit
    1,                      // Number pins
    &sudOutputPinDesktop    // Pin details
};


// List of class IDs and creator functions for the class factory. This
// provides the link between the OLE entry point in the DLL and an object
// being created. The class factory will call the static CreateInstance.
// We provide a set of filters in this one DLL.

CFactoryTemplate g_Templates[2] = 
{
    { 
      DS_FILTER_DESCRIPTION,               // Name
      &CLSID_PushSourceDesktop,       // CLSID
      CGameCapture::CreateInstance, // Method to create an instance of MyComponent
      NULL,                           // Initialization function
      &sudPushSourceDesktop           // Set-up information (for filters)
    }
};

int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);    


#define CreateComObject(clsid, iid, var) CoCreateInstance( clsid, NULL, CLSCTX_INPROC_SERVER, iid, (void **)&var);

STDAPI AMovieSetupRegisterServer(CLSID   clsServer, LPCWSTR szDescription, LPCWSTR szFileName, LPCWSTR szThreadingModel = L"Both", LPCWSTR szServerType = L"InprocServer32");

STDAPI AMovieSetupUnregisterServer( CLSID clsServer );

STDAPI RegisterFilters( BOOL bRegister )
{
    HRESULT hr = NOERROR;
    WCHAR achFileName[MAX_PATH];
    char achTemp[MAX_PATH];
    ASSERT(g_hInst != 0);

	if (0 == GetModuleFileNameA(g_hInst, achTemp, sizeof(achTemp))) {
		error("Failed to get module file name");
		return AmHresultFromWin32(GetLastError());
	}

    MultiByteToWideChar(CP_ACP, 0L, achTemp, lstrlenA(achTemp) + 1, 
                       achFileName, NUMELMS(achFileName));
  
    hr = CoInitialize(0);
	if (FAILED(hr)) {
		error("Failed to coinitialize %ld", hr);
	}

    if(bRegister)
    { 
		info("achFileName: %ls", achFileName);
		info("Registering movie setup server");
        hr = AMovieSetupRegisterServer(CLSID_PushSourceDesktop, L"gst-to-dshow", achFileName, L"Both", L"InprocServer32");

		if (FAILED(hr)) {
			error("Failed to AMovieSetupRegisterServer %ld", hr);
		}
    }

    if( SUCCEEDED(hr) )
    {
        IFilterMapper2 *fm = 0;
		info("Create FilterMapper2 COM Object");
        hr = CreateComObject( CLSID_FilterMapper2, IID_IFilterMapper2, fm );
        if( SUCCEEDED(hr) )
        {
            if(bRegister)
            {
                IMoniker *pMoniker = 0;
                REGFILTER2 rf2;
                rf2.dwVersion = 1;
                rf2.dwMerit = MERIT_DO_NOT_USE;
                rf2.cPins = 1;
                rf2.rgPins = &sudOutputPinDesktop;
				// this is the name that actually shows up in VLC et al. weird
				
                info("Registering %S", DS_FILTER_NAME);
                hr = fm->RegisterFilter(CLSID_PushSourceDesktop, DS_FILTER_NAME, &pMoniker, &CLSID_VideoInputDeviceCategory, NULL, &rf2);
				if (FAILED(hr)) {
					error("Failed to RegisterFilter %S %ld", DS_FILTER_NAME, hr);
				}
            }
            else
            {
                info("Unregistering %S", DS_FILTER_NAME);
                hr = fm->UnregisterFilter(&CLSID_VideoInputDeviceCategory, 0, CLSID_PushSourceDesktop);
				if (FAILED(hr)) {
					error("Failed to UnregisterFilter %S %ld", DS_FILTER_NAME, hr);
				}
            }
        }

      // release interface
      //
      if(fm)
          fm->Release();
    }

	if (SUCCEEDED(hr) && !bRegister) {
		info("Unregistering movie setup server");
		hr = AMovieSetupUnregisterServer(CLSID_PushSourceDesktop);

		if (FAILED(hr)) {
			error("Failed to AMovieSetupUnregisterServer %ld", hr);
		}
	}

    CoFreeUnusedLibraries();
    CoUninitialize();
	info("RegisterFilters Register: %d - DONE result: %x", bRegister, hr);
    return hr;
}
BOOL   HelperWriteKey(
	HKEY roothk,
	LPCWSTR lpSubKey,
	LPCTSTR val_name,
	DWORD dwType,
	void *lpvData,
	DWORD dwDataSize)
{
	//
	//Helper function for doing the registry write operations
	//
	//roothk:either of HKCR, HKLM, etc

	//lpSubKey: the key relative to 'roothk'

	//val_name:the key value name where the data will be written

	//dwType:the type of data that will be written ,REG_SZ,REG_BINARY, etc.

	//lpvData:a pointer to the data buffer

	//dwDataSize:the size of the data pointed to by lpvData
	//
	//

	info("Writing registry %ls: %ls", lpSubKey, lpvData);

	HKEY hk;
	if (ERROR_SUCCESS != RegCreateKey(roothk, lpSubKey, &hk)) return FALSE;

	if (ERROR_SUCCESS != RegSetValueExW(hk, val_name, 0, dwType, (CONST BYTE *)lpvData, dwDataSize)) return FALSE;

	if (ERROR_SUCCESS != RegCloseKey(hk))   return FALSE;
	return TRUE;

}

STDAPI DllRegisterServer()
{
	setupLogging();
    return RegisterFilters(TRUE); // && AMovieDllRegisterServer2( TRUE );
}

STDAPI DllUnregisterServer()
{
	setupLogging();
    return RegisterFilters(FALSE); // && AMovieDllRegisterServer2( FALSE );
}


//
// DllEntryPoint
//
extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);


BOOL APIENTRY DllMain(HANDLE hModule,
	DWORD  dwReason,
	LPVOID lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH) {
		g_hModule = (HMODULE)hModule;
	}

	return DllEntryPoint((HINSTANCE)(hModule), dwReason, lpReserved);
}