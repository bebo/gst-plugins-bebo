//------------------------------------------------------------------------------
// File: CaptureGuids.h
//
// Desc: DirectShow sample code - GUID definitions for PushSource filter set
//
// Copyright (c) Microsoft Corporation.  All rights reserved.
//------------------------------------------------------------------------------

#pragma once


#ifdef _WIN64
// b44bf41d - a061 - 4c8e - 8f03 - 783580c40f6c
DEFINE_GUID(CLSID_PushSourceDesktop, 0xb44bf41d, 0xa061, 0x4c8e, \
    0x8f, 0x03, 0x78, 0x35, 0x80, 0xc4, 0x0f, 0x6c);
#else
// f55fa525 - 30f4 - 45cb - 9fc8 - 6e51d5bf22d0
DEFINE_GUID(CLSID_PushSourceDesktop, 0xf55fa525, 0x30f4, 0x45cb, \
    0x9f, 0xc8, 0x6e, 0x51, 0xd5, 0xbf, 0x22, 0xd0);
#endif

// Filter name strings
#define DS_FILTER_DESCRIPTION    L"Bebo GStreamer to Direct Show Filter"
#define DS_FILTER_NAME           L"bebo-gst-to-dshow"
#define DS_LOG_NAME              "gst-to-dshow-filter"