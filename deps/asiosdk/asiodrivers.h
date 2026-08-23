/*
    ASIO Drivers - Driver enumeration and loading
    SDK Version 2.3
    (c) Steinberg Media Technologies GmbH
*/

#ifndef __asiodrivers_h__
#define __asiodrivers_h__

#include "asio.h"
#include <windows.h>
#include <objbase.h>

#ifdef __cplusplus
extern "C" {
#endif

// ASIO Driver enumeration
typedef struct ASIODriverInfo {
    CLSID clsid;
    char name[32];
    char description[128];
} ASIODriverInfo;

// Enumerate installed ASIO drivers
int32_t ASIOEnumDrivers(ASIODriverInfo* drivers, int32_t maxDrivers);

// Load ASIO driver by CLSID
ASIOError ASIOLoadDriver(const CLSID* clsid, ASIODriverInfo* info);

// Unload ASIO driver
void ASIOUnloadDriver();

// Get ASIO driver registry key
const char* ASIOGetRegistryKey();

#ifdef __cplusplus
}
#endif

#endif // __asiodrivers_h__
