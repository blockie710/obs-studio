/******************************************************************************
    Copyright (C) 2024 by Nexus Signalworks <contact@nexussignalworks.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include <asio.h>
#include "asio-manager.h"
#include <util/platform.h>
#include <util/dstr.h>
#include <obs-module.h>

#include <shlwapi.h>
#include <comdef.h>

#pragma comment(lib, "shlwapi.lib")

namespace win_asio {

DriverManager::DriverManager() {
    ensureCOMInitialized();
    enumerateDriversFromRegistry();
}

DriverManager::~DriverManager() {
    // Release all devices
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [clsid, handle] : devices_) {
        if (handle && handle->asio && handle->asio->driverInfo) {
            handle->asio->driverInfo->ASIODisposeBuffers();
            handle->asio->driverInfo->ASIOStop();
            handle->asio->release();
            handle->asio->driverInfo = nullptr;
        }
    }
    devices_.clear();
    uninitializeCOM();
}

void DriverManager::ensureCOMInitialized() {
    if (!comInitialized_) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr)) {
            comInitialized_ = true;
        } else {
            blog(LOG_WARNING, "[ASIO] COM initialization failed: 0x%08X", hr);
        }
    }
}

void DriverManager::uninitializeCOM() {
    if (comInitialized_) {
        CoUninitialize();
        comInitialized_ = false;
    }
}

void DriverManager::enumerateDriversFromRegistry() {
    cachedDrivers_.clear();
    
    HKEY hKey;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ, &hKey);
    if (result != ERROR_SUCCESS) {
        blog(LOG_INFO, "[ASIO] Registry key HKLM\\SOFTWARE\\ASIO not found");
        return;
    }
    
    DWORD index = 0;
    WCHAR subKeyName[256];
    DWORD subKeyNameSize;
    
    while (RegEnumKeyExW(hKey, index, subKeyName, &subKeyNameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        HKEY hSubKey;
        if (RegOpenKeyExW(hKey, subKeyName, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
            DriverInfo info;
            
            // Get CLSID
            WCHAR clsidStr[64];
            DWORD clsidSize = sizeof(clsidStr);
            if (RegGetValueW(hSubKey, nullptr, L"CLSID", RRF_RT_REG_SZ, nullptr, clsidStr, &clsidSize) == ERROR_SUCCESS) {
                CLSID clsid;
                if (SUCCEEDED(CLSIDFromString(clsidStr, &clsid))) {
                    info.clsid = clsid;
                }
            }
            
            // Get description
            WCHAR desc[256];
            DWORD descSize = sizeof(desc);
            if (RegGetValueW(hSubKey, nullptr, L"Description", RRF_RT_REG_SZ, nullptr, desc, &descSize) == ERROR_SUCCESS) {
                info.description = desc;
            }
            
            // Get path
            WCHAR path[MAX_PATH];
            DWORD pathSize = sizeof(path);
            if (RegGetValueW(hSubKey, nullptr, L"Path", RRF_RT_REG_SZ, nullptr, path, &pathSize) == ERROR_SUCCESS) {
                info.path = path;
            }
            
            // Use subkey name as driver name
            info.name = subKeyName;
            
            if (info.clsid.Data1 != 0) {
                cachedDrivers_.push_back(info);
                blog(LOG_INFO, "[ASIO] Found driver: %ls (CLSID: %ls)", info.name.c_str(), clsidStr);
            }
            
            RegCloseKey(hSubKey);
        }
        index++;
        subKeyNameSize = 256;
    }
    
    RegCloseKey(hKey);
    driversCached_ = true;
}

std::vector<DriverInfo> DriverManager::enumerateDrivers() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!driversCached_) {
        enumerateDriversFromRegistry();
    }
    return cachedDrivers_;
}

ASIOError DriverManager::loadDriver(const CLSID& clsid, DriverInfo& outInfo) {
    // Find driver in cache
    auto it = std::find_if(cachedDrivers_.begin(), cachedDrivers_.end(),
        [&clsid](const DriverInfo& d) { return IsEqualCLSID(d.clsid, clsid); });
    
    if (it == cachedDrivers_.end()) {
        return ASE_NotPresent;
    }
    
    // Load driver DLL
    HMODULE hModule = LoadLibraryW(it->path.c_str());
    if (!hModule) {
        blog(LOG_ERROR, "[ASIO] Failed to load driver DLL: %ls", it->path.c_str());
        return ASE_HWMalfunction;
    }
    
    // Get entry point
    typedef ASIOError (ASIO_CALLING_CONVENTION *ASIOEntryProc)(ASIODriverInfo*);
    ASIOEntryProc entry = (ASIOEntryProc)GetProcAddress(hModule, "ASIOEntry");
    if (!entry) {
        FreeLibrary(hModule);
        return ASE_InvalidParameter;
    }
    
    // Initialize driver - the driver fills in the ASIODriverInfo struct
    ASIODriverInfo* sdkInfo = new ASIODriverInfo();
    memset(sdkInfo, 0, sizeof(ASIODriverInfo));
    
    char nameAscii[32];
    wcstombs(nameAscii, it->name.c_str(), 32);
    strncpy_s(sdkInfo->name, sizeof(sdkInfo->name), nameAscii, _TRUNCATE);
    
    ASIOError err = entry(sdkInfo);
    if (err != ASE_OK) {
        delete sdkInfo;
        FreeLibrary(hModule);
        return err;
    }
    
    // Store the driver info in the handle
    outInfo = *it;
    outInfo.driverInfoPtr = sdkInfo;
    outInfo.driverModule = hModule;
    
    return ASE_OK;
}

void DriverManager::unloadDriver(const CLSID& clsid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(clsid);
    if (it != devices_.end()) {
        if (it->second && it->second->asio && it->second->asio->driverInfo) {
            it->second->asio->driverInfo->ASIODisposeBuffers();
            it->second->asio->driverInfo->ASIOStop();
            it->second->asio->release();
            it->second->asio->driverInfo = nullptr;
        }
        devices_.erase(it);
    }
}

std::shared_ptr<DeviceHandle> DriverManager::acquireDevice(const CLSID& clsid) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = devices_.find(clsid);
    if (it != devices_.end()) {
        // Already loaded, increment ref count
        it->second->refCount.fetch_add(1, std::memory_order_relaxed);
        return it->second;
    }
    
    // Find driver info
    auto driverIt = std::find_if(cachedDrivers_.begin(), cachedDrivers_.end(),
        [&clsid](const DriverInfo& d) { return IsEqualCLSID(d.clsid, clsid); });
    
    if (driverIt == cachedDrivers_.end()) {
        return nullptr;
    }
    
    // Load driver
    DriverInfo driverInfo = {};
    ASIOError err = loadDriver(clsid, driverInfo);
    if (err != ASE_OK) {
        blog(LOG_ERROR, "[ASIO] Failed to load driver: 0x%08X", err);
        return nullptr;
    }
    
    // Create device handle
    auto handle = std::make_shared<DeviceHandle>();
    handle->driverInfo = *driverIt;
    handle->refCount.store(1, std::memory_order_relaxed);
    handle->asio = new IASIO();
    handle->asio->driverInfo = driverInfo.driverInfoPtr;
    
    // Store in map
    devices_[clsid] = handle;
    
    return handle;
}

void DriverManager::releaseDevice(const CLSID& clsid) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = devices_.find(clsid);
    if (it != devices_.end()) {
        int newCount = it->second->refCount.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (newCount <= 0) {
            // Last reference, cleanup
            if (it->second && it->second->asio && it->second->asio->driverInfo) {
                it->second->asio->driverInfo->ASIODisposeBuffers();
                it->second->asio->driverInfo->ASIOStop();
                it->second->asio->release();
                
                // Free the driver info
                if (it->second->asio->driverInfo) {
                    delete it->second->asio->driverInfo;
                    it->second->asio->driverInfo = nullptr;
                }
                
                // Free the driver module
                if (it->second->driverInfo.driverModule) {
                    FreeLibrary(it->second->driverInfo.driverModule);
                    it->second->driverInfo.driverModule = nullptr;
                }
            }
            
            // Free the IASIO wrapper
            if (it->second->asio) {
                delete it->second->asio;
                it->second->asio = nullptr;
            }
            devices_.erase(it);
        }
    }
}

bool DriverManager::showControlPanel(const CLSID& clsid, HWND parent) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = devices_.find(clsid);
    if (it == devices_.end()) {
        // Try to acquire temporarily
        auto handle = acquireDevice(clsid);
        if (!handle || !handle->asio || !handle->asio->driverInfo) {
            return false;
        }
        
        ASIOError err = handle->asio->driverInfo->ASIOControlPanel();
        releaseDevice(clsid);
        return err == ASE_OK;
    }
    
    if (!it->second || !it->second->asio || !it->second->asio->driverInfo) {
        return false;
    }
    
    ASIOError err = it->second->asio->driverInfo->ASIOControlPanel();
    return err == ASE_OK;
}

bool DriverManager::getDeviceInfo(const CLSID& clsid, DriverInfo& outInfo) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = std::find_if(cachedDrivers_.begin(), cachedDrivers_.end(),
        [&clsid](const DriverInfo& d) { return IsEqualCLSID(d.clsid, clsid); });
    
    if (it != cachedDrivers_.end()) {
        outInfo = *it;
        return true;
    }
    return false;
}

} // namespace win_asio