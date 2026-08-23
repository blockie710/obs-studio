/******************************************************************************
    Copyright (C) 2025 by OBS Studio Contributors.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#pragma once

// Include ASIO SDK types
#include <asio.h>
#include <obs-module.h>

#include <mutex>
#include <atomic>
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <windows.h>
#include <objbase.h>

#include "asio-ringbuffer.h"
#include "asio-resampler.h"

namespace win_asio {

// Our wrapper types in win_asio namespace
struct DriverInfo {
    CLSID clsid;
    std::wstring name;
    std::wstring description;
    std::wstring path;
    
    // Runtime driver info (set after loading)
    ASIODriverInfo* driverInfoPtr = nullptr;
    HMODULE driverModule = nullptr;
};

struct DeviceHandle {
    IASIO* asio = nullptr;
    std::atomic<int> refCount{0};
    DriverInfo driverInfo;
    ASIOBufferInfo bufferInfo[64];
    ASIOChannelInfo channelInfo[64];
    int32_t sampleRate = 0;
    int32_t bufferSize = 0;
    int32_t numInputChannels = 0;
    int32_t numOutputChannels = 0;
    bool running = false;
    
    ASIOCallbacks callbacks{};
    
    // Ring buffer for audio data (per channel)
    RingBuffer<float, 8192>* ringBuffers = nullptr;
    
    // Resampler context
    ResamplerContext* resampler = nullptr;
    
    // Format conversion
    ASIOSampleType nativeFormat = ASIOSTFloat32LSB;
    int32_t nativeChannels = 0;
    
    // XRun counter
    std::atomic<uint64_t> xrunCount{0};
};

class DriverManager {
public:
    static DriverManager& instance() {
        static DriverManager instance;
        return instance;
    }
    
    DriverManager(const DriverManager&) = delete;
    DriverManager& operator=(const DriverManager&) = delete;
    
    // Driver discovery
    std::vector<DriverInfo> enumerateDrivers();
    
    // Device handle management
    std::shared_ptr<DeviceHandle> acquireDevice(const CLSID& clsid);
    void releaseDevice(const CLSID& clsid);
    
    // Control panel
    bool showControlPanel(const CLSID& clsid, HWND parent = nullptr);
    
    // Get device info without acquiring
    bool getDeviceInfo(const CLSID& clsid, DriverInfo& outInfo);

private:
    DriverManager();
    ~DriverManager();
    
    std::mutex mutex_;
    struct CLSIDLess {
        bool operator()(const CLSID& a, const CLSID& b) const {
            return memcmp(&a, &b, sizeof(CLSID)) < 0;
        }
    };
    std::map<CLSID, std::shared_ptr<DeviceHandle>, CLSIDLess> devices_;
    std::vector<DriverInfo> cachedDrivers_;
    bool driversCached_ = false;
    
    // COM initialization
    bool comInitialized_ = false;
    void ensureCOMInitialized();
    void uninitializeCOM();
    
    // Registry enumeration
    void enumerateDriversFromRegistry();
    
    // Load driver DLL and get entry point
    ASIOError loadDriver(const CLSID& clsid, DriverInfo& outInfo);
    void unloadDriver(const CLSID& clsid);
};

} // namespace win_asio