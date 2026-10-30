/******************************************************************************
 * win-asio: Native Windows ASIO Audio Capture Plugin
 *
 * Copyright (C) 2024 Community Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *****************************************************************************/

#pragma once

#include <obs-module.h>
#include <util/threading.h>

#include <windows.h>
#include <combaseapi.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>

#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <functional>

// ASIO SDK types (loaded dynamically - no static linking)
using ASIOBool = long;
using ASIOSampleRate = double;
using ASIOSamples = long long;
using ASIOTimeStamp = long long;

struct ASIOTime {
    ASIOTimeStamp time;
    ASIOSamples samplePosition;
    ASIOSamples samplePositionHi;
    ASIOTimeStamp systemTime;
    ASIOTimeStamp systemTimeHi;
    long flags;
};

typedef struct ASIODriverInfo {
    char name[64];
    char version[64];
    char vendor[64];
    char url[128];
    long asioVersion;
    long driverVersion;
    long inputChannels;
    long outputChannels;
} ASIODriverInfo;

typedef struct ASIOChannelInfo {
    long channel;
    long isInput;
    long isActive;
    long channelGroup;
    long type;
    char name[32];
} ASIOChannelInfo;

typedef struct ASIOBufferInfo {
    bool isInput;
    long channelNum;
    void* buffers[2];  // double buffer
} ASIOBufferInfo;

typedef struct ASIOCallbacks {
    void (*bufferSwitch)(long index, ASIOBool processNow);
    void (*sampleRateDidChange)(ASIOSampleRate sRate);
    long (*asioMessage)(long selector, long value, void* message, double* opt);
    ASIOTime* (*bufferSwitchTimeInfo)(ASIOTime* params, long index, ASIOBool processNow);
} ASIOCallbacks;

typedef void (*ASIOExit)();
typedef long (*ASIOInit)(ASIODriverInfo* info);
typedef long (*ASIOGetChannels)(long* numInputChannels, long* numOutputChannels);
typedef long (*ASIOGetLatencies)(long* inputLatency, long* outputLatency);
typedef long (*ASIOGetBufferSize)(long* minSize, long* maxSize, long* preferredSize, long* granularity);
typedef long (*ASIOCanSampleRate)(ASIOSampleRate sampleRate);
typedef long (*ASIOGetSampleRate)(ASIOSampleRate* sampleRate);
typedef long (*ASIOSetSampleRate)(ASIOSampleRate sampleRate);
typedef long (*ASIOGetChannelInfo)(ASIOChannelInfo* info);
typedef long (*ASIOCreateBuffers)(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks);
typedef long (*ASIODisposeBuffers)();
typedef long (*ASIOControlPanel)();
typedef long (*ASIOFuture)(long selector, void* opt);
typedef long (*ASIOOutputReady)();
typedef long (*ASIOStart)();
typedef long (*ASIOStop)();
typedef long (*ASIOGetSamplePosition)(ASIOSamples* sPos, ASIOTimeStamp* tStamp);

// ASIO Sample Types
enum ASIOSampleType {
    ASIOSTInt16MSB    = 0,
    ASIOSTInt24MSB    = 1,
    ASIOSTInt32MSB    = 2,
    ASIOSTFloat32MSB  = 3,
    ASIOSTFloat64MSB  = 4,
    ASIOSTInt32MSB16  = 8,
    ASIOSTInt32MSB18  = 9,
    ASIOSTInt32MSB20  = 10,
    ASIOSTInt32MSB24  = 11,
    ASIOSTInt16LSB    = 16,
    ASIOSTInt24LSB    = 17,
    ASIOSTInt32LSB    = 18,
    ASIOSTFloat32LSB  = 19,
    ASIOSTFloat64LSB  = 20,
    ASIOSTInt32LSB16  = 24,
    ASIOSTInt32LSB18  = 25,
    ASIOSTInt32LSB20  = 26,
    ASIOSTInt32LSB24  = 27,
    ASIOSTDSDInt8LSB1 = 32,
    ASIOSTDSDInt8MSB1 = 33,
    ASIOSTDSDInt8NER8 = 40
};

// ASIO COM Interface
struct IASIO {
    virtual long QueryInterface(const IID& riid, void** ppv) = 0;
    virtual long AddRef() = 0;
    virtual long Release() = 0;

    virtual long Init(ASIODriverInfo* info) = 0;
    virtual long GetChannels(long* numInputChannels, long* numOutputChannels) = 0;
    virtual long GetLatencies(long* inputLatency, long* outputLatency) = 0;
    virtual long GetBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) = 0;
    virtual long CanSampleRate(ASIOSampleRate sampleRate) = 0;
    virtual long GetSampleRate(ASIOSampleRate* sampleRate) = 0;
    virtual long SetSampleRate(ASIOSampleRate sampleRate) = 0;
    virtual long GetChannelInfo(ASIOChannelInfo* info) = 0;
    virtual long CreateBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks) = 0;
    virtual long DisposeBuffers() = 0;
    virtual long ControlPanel() = 0;
    virtual long Future(long selector, void* opt) = 0;
    virtual long OutputReady() = 0;
    virtual long Start() = 0;
    virtual long Stop() = 0;
    virtual long GetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) = 0;
    virtual long GetChannelName(long channel, bool isInput, char* name, long nameSize) = 0;
    virtual long GetSampleRateRange(ASIOSampleRate* min, ASIOSampleRate* max) = 0;
};

// Utility function for wide string conversion
namespace win_asio {
std::string wstr_to_str(const std::wstring& wstr);
std::wstring str_to_wstr(const std::string& str);
}

// Include the ring buffer class (outside namespace)
#include "asio-ring-buffer.hpp"

namespace win_asio {

// ASIO Sample Types
enum ASIOSampleType {
    ASIOSTInt16MSB    = 0,
    ASIOSTInt24MSB    = 1,
    ASIOSTInt32MSB    = 2,
    ASIOSTFloat32MSB  = 3,
    ASIOSTFloat64MSB  = 4,
    ASIOSTInt32MSB16  = 8,
    ASIOSTInt32MSB18  = 9,
    ASIOSTInt32MSB20  = 10,
    ASIOSTInt32MSB24  = 11,
    ASIOSTInt16LSB    = 16,
    ASIOSTInt24LSB    = 17,
    ASIOSTInt32LSB    = 18,
    ASIOSTFloat32LSB  = 19,
    ASIOSTFloat64LSB  = 20,
    ASIOSTInt32LSB16  = 24,
    ASIOSTInt32LSB18  = 25,
    ASIOSTInt32LSB20  = 26,
    ASIOSTInt32LSB24  = 27,
    ASIOSTDSDInt8LSB1 = 32,
    ASIOSTDSDInt8MSB1 = 33,
    ASIOSTDSDInt8NER8 = 40
};

// Forward declarations
struct asio_driver_t;
struct asio_channel_router_t;

struct asio_driver_t {
    IASIO* com_interface = nullptr;
    HMODULE dll_handle = nullptr;

    ASIODriverInfo info = {};
    std::vector<ASIOChannelInfo> input_channels;
    std::vector<ASIOChannelInfo> output_channels;

    long preferred_buffer_size = 0;
    double current_sample_rate = 0.0;

    // Multi-client reference counting
    std::atomic<int> ref_count{0};
    std::mutex config_mutex;

    // Audio callback context
    ASIOCallbacks callbacks = {};

    // Channel routing
    std::unique_ptr<asio_channel_router_t> router;

    // State
    std::atomic<bool> is_running{false};
    std::atomic<bool> buffers_created{false};
};

struct asio_channel_router_t {
    // Source: ASIO hardware channels (interleaved or planar)
    // Dest: OBS sources (planar float32)

    struct route_t {
        int asio_channel;      // ASIO hardware channel index
        int obs_channel;       // OBS channel index (0..N-1)
        float gain = 1.0f;     // Per-route gain
        bool invert = false;   // Phase inversion
    };

    std::vector<route_t> input_routes;
    std::vector<route_t> output_routes;

    // Lock-free routing tables (updated atomically on config change)
    std::atomic<int> input_route_version{0};
    std::atomic<int> output_route_version{0};

    // SIMD-optimized deinterleave/planar conversion
    void (*deinterleave_to_planar)(const void* src, float** dst,
                                    int num_frames, int num_channels,
                                    const route_t* routes, int num_routes);
    void (*interleave_from_planar)(const float** src, void* dst,
                                    int num_frames, int num_channels,
                                    const route_t* routes, int num_routes);
};

/**
 * ASIO Driver Manager - Singleton owning the active hardware handle
 * Multiplexes sub-channel routing across multiple OBS scene sources
 */
class ASIO_DriverManager {
public:
    static ASIO_DriverManager& Instance() {
        static ASIO_DriverManager instance;
        return instance;
    }

    ~ASIO_DriverManager();

    // Non-copyable, non-movable
    ASIO_DriverManager(const ASIO_DriverManager&) = delete;
    ASIO_DriverManager& operator=(const ASIO_DriverManager&) = delete;

    // Dynamic driver discovery via Windows Registry
    struct DriverDescriptor {
        std::string clsid;           // {xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}
        std::string name;            // Human-readable name
        std::string dll_path;        // Path to ASIO driver DLL
        std::string vendor;
        std::string version;
        long input_channels = 0;
        long output_channels = 0;
        long asio_version = 0;
    };

    // Discover all installed ASIO drivers
    std::vector<DriverDescriptor> DiscoverDrivers();

    // Open/close driver by CLSID
    bool OpenDriver(const std::string& driver_clsid);
    void CloseDriver();

    // Reference-counted client registration
    int AcquireClient();      // Returns client ID
    void ReleaseClient(int client_id);

    // Configuration
    bool ConfigureChannels(const std::vector<int>& input_channels,
                           const std::vector<int>& output_channels,
                           int buffer_size, double sample_rate);
    bool Start();
    bool Stop();

    // Audio callback registration
    using AudioCallback = std::function<void(float** input_planar, float** output_planar,
                                             int num_frames, int num_input_channels,
                                             int num_output_channels, double timestamp)>;
    void SetAudioCallback(int client_id, AudioCallback callback);

    // Channel routing per client
    bool SetClientInputRouting(int client_id, const std::vector<asio_channel_router_t::route_t>& routes);
    bool SetClientOutputRouting(int client_id, const std::vector<asio_channel_router_t::route_t>& routes);

    // Status
    bool IsRunning() const { return driver_ && driver_->is_running.load(); }
    const ASIODriverInfo* GetDriverInfo() const { return driver_ ? &driver_->info : nullptr; }
    int GetBufferSize() const { return driver_ ? driver_->preferred_buffer_size : 0; }
    double GetSampleRate() const { return driver_ ? driver_->current_sample_rate : 0.0; }
    int GetInputChannelCount() const { return driver_ ? (int)driver_->input_channels.size() : 0; }
    int GetOutputChannelCount() const { return driver_ ? (int)driver_->output_channels.size() : 0; }

    // Control Panel
    bool OpenControlPanel();

    // Statistics
    struct Stats {
        uint64_t frames_processed = 0;
        uint64_t overruns = 0;
        uint64_t underruns = 0;
        double current_cpu_load = 0.0;
    };
    Stats GetStats() const;

private:
    ASIO_DriverManager();
    void InitializeCOM();
    void UninitializeCOM();

    bool LoadDriverDLL(const std::string& dll_path);
    bool CreateASIOInstance(const std::string& clsid);
    void DestroyASIOInstance();

    static void CALLBACK BufferSwitchCallback(long index, ASIOBool processNow);
    static void CALLBACK SampleRateChangeCallback(ASIOSampleRate sRate);
    static long CALLBACK AsioMessageCallback(long selector, long value, void* message, double* opt);
    static ASIOTime* CALLBACK BufferSwitchTimeInfoCallback(ASIOTime* params, long index, ASIOBool processNow);

    void ProcessAudioCallback(long buffer_index, bool process_now);

    // Internal state
    std::unique_ptr<asio_driver_t> driver_;
    std::mutex driver_mutex_;

    // Client management
    mutable std::mutex clients_mutex_;
    struct ClientContext {
        int id;
        AudioCallback callback;
        std::vector<asio_channel_router_t::route_t> input_routes;
        std::vector<asio_channel_router_t::route_t> output_routes;
        std::unique_ptr<ASIORingBuffer> input_buffer;
        std::unique_ptr<ASIORingBuffer> output_buffer;
    };
    std::unordered_map<int, std::unique_ptr<ClientContext>> clients_;
    std::atomic<int> next_client_id_{1};

    // COM initialization state
    bool com_initialized_ = false;
};

} // namespace win_asio