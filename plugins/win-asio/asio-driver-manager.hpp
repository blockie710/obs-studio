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
#include <util/circlebuf.h>

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

// ASIO SDK types (loaded dynamically - no static linking)
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

// ASIO COM Interface
struct IASIO {
    virtual long QueryInterface(const IID& riid, void** ppv) = 0;
    virtual long AddRef() = 0;
    long Release() = 0;

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

// Forward declarations
struct asio_driver_t;
struct asio_channel_router_t;

/**
 * Lock-free SPSC ring buffer for real-time audio transport
 * Zero-allocation, cache-line aligned for NUMA-friendly access
 */
struct asio_ring_buffer_t {
    // Cache-line alignment to prevent false sharing
    alignas(64) std::atomic<size_t> write_pos{0};
    alignas(64) std::atomic<size_t> read_pos{0};
    alignas(64) size_t capacity;
    alignas(64) size_t channel_count;
    alignas(64) size_t frame_stride;  // bytes per frame (all channels)
    alignas(64) uint8_t* data;

    // Statistics (updated atomically)
    alignas(64) std::atomic<uint64_t> frames_written{0};
    alignas(64) std::atomic<uint64_t> frames_read{0};
    alignas(64) std::atomic<uint64_t> overruns{0};
    alignas(64) std::atomic<uint64_t> underruns{0};

    // Pre-allocated planar float32 buffers per channel
    // [channel][frame * sizeof(float)]
};

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
    std::mutex clients_mutex_;
    struct ClientContext {
        int id;
        AudioCallback callback;
        std::vector<asio_channel_router_t::route_t> input_routes;
        std::vector<asio_channel_router_t::route_t> output_routes;
        std::unique_ptr<asio_ring_buffer_t> input_buffer;
        std::unique_ptr<asio_ring_buffer_t> output_buffer;
    };
    std::unordered_map<int, std::unique_ptr<ClientContext>> clients_;
    std::atomic<int> next_client_id_{1};

    // COM initialization state
    bool com_initialized_ = false;
};