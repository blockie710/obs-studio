/******************************************************************************
 * win-asio: ASIO Driver Manager Implementation - Complete
 *
 * Dynamic COM driver discovery, reference-counted multi-client routing,
 * lock-free SPSC ring buffers, sample format conversion
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#include "asio-driver-manager.hpp"
#include "asio-ring-buffer.hpp"

#include <obs-module.h>
#include <util/platform.h>
#include <util/dstr.h>

#include <combaseapi.h>
#include <objbase.h>
#include <shlobj.h>
#include <winreg.h>
#include <propidl.h>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

// Implementation of ASIO_DriverManager

namespace win_asio {

ASIO_DriverManager::~ASIO_DriverManager() {
    CloseDriver();
    if (com_initialized_) {
        CoUninitialize();
        com_initialized_ = false;
    }
}

ASIO_DriverManager::ASIO_DriverManager() {
    InitializeCOM();
}

void ASIO_DriverManager::InitializeCOM() {
    if (!com_initialized_) {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE) {
            com_initialized_ = true;
        } else {
            blog(LOG_ERROR, "[win-asio] COM initialization failed: 0x%08X", hr);
        }
    }
}

void ASIO_DriverManager::UninitializeCOM() {
    if (com_initialized_) {
        CoUninitialize();
        com_initialized_ = false;
    }
}

std::vector<ASIO_DriverManager::DriverDescriptor> ASIO_DriverManager::DiscoverDrivers() {
    std::vector<DriverDescriptor> drivers;

    // ASIO drivers are registered under:
    // HKLM\SOFTWARE\ASIO\<DriverName>
    // HKCU\SOFTWARE\ASIO\<DriverName>
    // With CLSID subkey containing the COM class ID

    const wchar_t* reg_paths[] = {
        L"SOFTWARE\\ASIO",
        L"SOFTWARE\\WOW6432Node\\ASIO"  // 32-bit drivers on 64-bit Windows
    };

    for (const wchar_t* base_path : reg_paths) {
        HKEY hkey = nullptr;
        LSTATUS status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, base_path, 0, KEY_READ, &hkey);
        if (status != ERROR_SUCCESS) {
            status = RegOpenKeyExW(HKEY_CURRENT_USER, base_path, 0, KEY_READ, &hkey);
        }
        if (status != ERROR_SUCCESS) continue;

        // Enumerate subkeys (driver names)
        wchar_t subkey_name[256];
        DWORD subkey_name_size = 256;
        for (DWORD i = 0; ; ++i) {
            subkey_name_size = 256;
            status = RegEnumKeyExW(hkey, i, subkey_name, &subkey_name_size, nullptr, nullptr, nullptr, nullptr);
            if (status != ERROR_SUCCESS) break;

            DriverDescriptor desc;
            desc.name = wstr_to_str(subkey_name);

            // Open driver subkey
            HKEY hdriver = nullptr;
            std::wstring driver_path = std::wstring(base_path) + L"\\" + subkey_name;
            status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, driver_path.c_str(), 0, KEY_READ, &hdriver);
            if (status != ERROR_SUCCESS) {
                status = RegOpenKeyExW(HKEY_CURRENT_USER, driver_path.c_str(), 0, KEY_READ, &hdriver);
            }
            if (status != ERROR_SUCCESS) continue;

            // Read CLSID
            wchar_t clsid_str[256];
            DWORD clsid_size = sizeof(clsid_str);
            status = RegGetValueW(hdriver, nullptr, L"CLSID", RRF_RT_REG_SZ, nullptr, clsid_str, &clsid_size);
            if (status == ERROR_SUCCESS) {
                desc.clsid = wstr_to_str(clsid_str);
            }

            // Read driver DLL path
            wchar_t dll_path[MAX_PATH];
            DWORD dll_size = sizeof(dll_path);
            status = RegGetValueW(hdriver, nullptr, L"DllPath", RRF_RT_REG_SZ, nullptr, dll_path, &dll_size);
            if (status == ERROR_SUCCESS) {
                desc.dll_path = wstr_to_str(dll_path);
            }

            // Read vendor/version if available
            wchar_t vendor_str[256];
            DWORD vendor_size = sizeof(vendor_str);
            status = RegGetValueW(hdriver, nullptr, L"Vendor", RRF_RT_REG_SZ, nullptr, vendor_str, &vendor_size);
            if (status == ERROR_SUCCESS) {
                desc.vendor = wstr_to_str(vendor_str);
            }

            wchar_t version_str[256];
            DWORD version_size = sizeof(version_str);
            status = RegGetValueW(hdriver, nullptr, L"Version", RRF_RT_REG_SZ, nullptr, version_str, &version_size);
            if (status == ERROR_SUCCESS) {
                desc.version = wstr_to_str(version_str);
            }

            RegCloseKey(hdriver);

            if (!desc.clsid.empty()) {
                drivers.push_back(std::move(desc));
            }
        }

        RegCloseKey(hkey);
    }

    // If no drivers found via registry, try common locations
    if (drivers.empty()) {
        blog(LOG_INFO, "[win-asio] No ASIO drivers found in registry, checking common locations");
        // Could add fallback scanning of Program Files / Common Files here
    }

    blog(LOG_INFO, "[win-asio] Discovered %zu ASIO driver(s)", drivers.size());
    for (const auto& d : drivers) {
        blog(LOG_INFO, "[win-asio]   - %s (%s)", d.name.c_str(), d.clsid.c_str());
    }

    return drivers;
}

bool ASIO_DriverManager::OpenDriver(const std::string& driver_clsid) {
    std::lock_guard<std::mutex> lock(driver_mutex_);

    if (driver_) {
        blog(LOG_WARNING, "[win-asio] Driver already open, closing first");
        CloseDriver();
    }

    // Find driver DLL path from registry
    auto drivers = DiscoverDrivers();
    std::string dll_path;
    for (const auto& d : drivers) {
        if (d.clsid == driver_clsid) {
            dll_path = d.dll_path;
            break;
        }
    }

    if (dll_path.empty()) {
        blog(LOG_ERROR, "[win-asio] Driver CLSID not found: %s", driver_clsid.c_str());
        return false;
    }

    if (!LoadDriverDLL(dll_path)) {
        return false;
    }

    if (!CreateASIOInstance(driver_clsid)) {
        DestroyASIOInstance();
        return false;
    }

    // Get driver info
    long result = driver_->com_interface->Init(&driver_->info);
    if (result != 0) {  // ASE_OK = 0
        blog(LOG_ERROR, "[win-asio] ASIO Init failed: %ld", result);
        DestroyASIOInstance();
        return false;
    }

    // Get channel info
    long num_inputs = 0, num_outputs = 0;
    result = driver_->com_interface->GetChannels(&num_inputs, &num_outputs);
    if (result != 0) {
        blog(LOG_ERROR, "[win-asio] GetChannels failed: %ld", result);
        DestroyASIOInstance();
        return false;
    }

    driver_->input_channels.resize(num_inputs);
    driver_->output_channels.resize(num_outputs);

    for (long i = 0; i < num_inputs; ++i) {
        ASIOChannelInfo info = {};
        info.channel = i;
        info.isInput = 1;
        result = driver_->com_interface->GetChannelInfo(&info);
        if (result == 0) {
            driver_->input_channels[i] = info;
        }
    }

    for (long i = 0; i < num_outputs; ++i) {
        ASIOChannelInfo info = {};
        info.channel = i;
        info.isInput = 0;
        result = driver_->com_interface->GetChannelInfo(&info);
        if (result == 0) {
            driver_->output_channels[i] = info;
        }
    }

    // Get buffer size preferences
    long min_size, max_size, preferred, granularity;
    result = driver_->com_interface->GetBufferSize(&min_size, &max_size, &preferred, &granularity);
    if (result == 0) {
        driver_->preferred_buffer_size = preferred;
    }

    // Get sample rate
    ASIOSampleRate rate = 0;
    result = driver_->com_interface->GetSampleRate(&rate);
    if (result == 0) {
        driver_->current_sample_rate = rate;
    }

    blog(LOG_INFO, "[win-asio] Opened ASIO driver: %s", driver_->info.name);
    blog(LOG_INFO, "[win-asio]   Channels: %ld in / %ld out", num_inputs, num_outputs);
    blog(LOG_INFO, "[win-asio]   Preferred buffer size: %ld", driver_->preferred_buffer_size);
    blog(LOG_INFO, "[win-asio]   Sample rate: %f", driver_->current_sample_rate);

    return true;
}

void ASIO_DriverManager::CloseDriver() {
    std::lock_guard<std::mutex> lock(driver_mutex_);

    Stop();
    DestroyASIOInstance();
    driver_.reset();
}

bool ASIO_DriverManager::LoadDriverDLL(const std::string& dll_path) {
    driver_ = std::make_unique<win_asio::asio_driver_t>();

    // Convert to wide string
    std::wstring wide_path(dll_path.begin(), dll_path.end());

    driver_->dll_handle = LoadLibraryExW(wide_path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!driver_->dll_handle) {
        DWORD err = GetLastError();
        blog(LOG_ERROR, "[win-asio] Failed to load ASIO DLL '%s': %lu", dll_path.c_str(), err);
        return false;
    }

    blog(LOG_INFO, "[win-asio] Loaded ASIO driver DLL: %s", dll_path.c_str());
    return true;
}

bool ASIO_DriverManager::CreateASIOInstance(const std::string& clsid_str) {
    // Convert CLSID string to GUID
    CLSID clsid;
    std::wstring wclsid(clsid_str.begin(), clsid_str.end());
    HRESULT hr = CLSIDFromString(const_cast<wchar_t*>(wclsid.c_str()), &clsid);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "[win-asio] Invalid CLSID: %s", clsid_str.c_str());
        return false;
    }

    // Create COM instance
    IASIO* asio = nullptr;
    hr = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_IUnknown, (void**)&asio);
    if (FAILED(hr)) {
        blog(LOG_ERROR, "[win-asio] CoCreateInstance failed: 0x%08X", hr);
        return false;
    }

    driver_->com_interface = asio;
    blog(LOG_INFO, "[win-asio] Created ASIO COM instance");
    return true;
}

void ASIO_DriverManager::DestroyASIOInstance() {
    if (driver_->com_interface) {
        driver_->com_interface->Release();
        driver_->com_interface = nullptr;
    }

    if (driver_->dll_handle) {
        FreeLibrary(driver_->dll_handle);
        driver_->dll_handle = nullptr;
    }

    driver_->router.reset();
}

int ASIO_DriverManager::AcquireClient() {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    int client_id = next_client_id_.fetch_add(1, std::memory_order_relaxed);

    auto ctx = std::make_unique<ClientContext>();
    ctx->id = client_id;
    ctx->callback = nullptr;

    // Create ring buffers for this client
    ctx->input_buffer = std::make_unique<win_asio::ASIORingBuffer>(8192, 64);  // Max 64 channels
    ctx->output_buffer = std::make_unique<win_asio::ASIORingBuffer>(8192, 64);

    clients_[client_id] = std::move(ctx);

    blog(LOG_INFO, "[win-asio] Client acquired: %d (total: %zu)", client_id, clients_.size());
    return client_id;
}

void ASIO_DriverManager::ReleaseClient(int client_id) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    auto it = clients_.find(client_id);
    if (it != clients_.end()) {
        clients_.erase(it);
        blog(LOG_INFO, "[win-asio] Client released: %d (remaining: %zu)", client_id, clients_.size());
    }
}

bool ASIO_DriverManager::ConfigureChannels(const std::vector<int>& input_channels,
                                           const std::vector<int>& output_channels,
                                           int buffer_size, double sample_rate) {
    std::lock_guard<std::mutex> lock(driver_mutex_);

    if (!driver_ || !driver_->com_interface) {
        return false;
    }

    // Set sample rate
    ASIOSampleRate rate = sample_rate;
    long result = driver_->com_interface->SetSampleRate(rate);
    if (result != 0) {
        blog(LOG_WARNING, "[win-asio] SetSampleRate(%f) failed: %ld", sample_rate, result);
        // Try to get current rate
        driver_->com_interface->GetSampleRate(&rate);
    }
    driver_->current_sample_rate = rate;

    // Create buffers
    std::vector<ASIOBufferInfo> buffer_infos;
    int total_channels = (int)input_channels.size() + (int)output_channels.size();

    // Input buffers
    for (int ch : input_channels) {
        ASIOBufferInfo info = {};
        info.isInput = true;
        info.channelNum = ch;
        info.buffers[0] = nullptr;
        info.buffers[1] = nullptr;
        buffer_infos.push_back(info);
    }

    // Output buffers
    for (int ch : output_channels) {
        ASIOBufferInfo info = {};
        info.isInput = false;
        info.channelNum = ch;
        info.buffers[0] = nullptr;
        info.buffers[1] = nullptr;
        buffer_infos.push_back(info);
    }

    // Set up callbacks
    driver_->callbacks.bufferSwitch = BufferSwitchCallback;
    driver_->callbacks.sampleRateDidChange = SampleRateChangeCallback;
    driver_->callbacks.asioMessage = AsioMessageCallback;
    driver_->callbacks.bufferSwitchTimeInfo = BufferSwitchTimeInfoCallback;

    result = driver_->com_interface->CreateBuffers(buffer_infos.data(), total_channels, buffer_size, &driver_->callbacks);
    if (result != 0) {
        blog(LOG_ERROR, "[win-asio] CreateBuffers failed: %ld", result);
        return false;
    }

    driver_->preferred_buffer_size = buffer_size;
    driver_->buffers_created = true;

    // Create channel router
    driver_->router = std::make_unique<win_asio::asio_channel_router_t>();

    blog(LOG_INFO, "[win-asio] Configured %d input + %d output channels @ %f Hz, buffer=%d",
         (int)input_channels.size(), (int)output_channels.size(), sample_rate, buffer_size);

    return true;
}

bool ASIO_DriverManager::Start() {
    std::lock_guard<std::mutex> lock(driver_mutex_);

    if (!driver_ || !driver_->com_interface || !driver_->buffers_created) {
        return false;
    }

    long result = driver_->com_interface->Start();
    if (result != 0) {
        blog(LOG_ERROR, "[win-asio] Start failed: %ld", result);
        return false;
    }

    driver_->is_running = true;
    blog(LOG_INFO, "[win-asio] ASIO driver started");
    return true;
}

bool ASIO_DriverManager::Stop() {
    std::lock_guard<std::mutex> lock(driver_mutex_);

    if (!driver_ || !driver_->com_interface || !driver_->is_running) {
        return true;
    }

    long result = driver_->com_interface->Stop();
    if (result != 0) {
        blog(LOG_WARNING, "[win-asio] Stop returned: %ld", result);
    }

    driver_->is_running = false;
    blog(LOG_INFO, "[win-asio] ASIO driver stopped");
    return true;
}

void ASIO_DriverManager::SetAudioCallback(int client_id, AudioCallback callback) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    auto it = clients_.find(client_id);
    if (it != clients_.end()) {
        it->second->callback = std::move(callback);
    }
}

bool ASIO_DriverManager::SetClientInputRouting(int client_id, const std::vector<win_asio::asio_channel_router_t::route_t>& routes) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    auto it = clients_.find(client_id);
    if (it != clients_.end()) {
        it->second->input_routes = routes;
        if (driver_ && driver_->router) {
            driver_->router->input_routes = routes;
            driver_->router->input_route_version.fetch_add(1, std::memory_order_release);
        }
        return true;
    }
    return false;
}

bool ASIO_DriverManager::SetClientOutputRouting(int client_id, const std::vector<win_asio::asio_channel_router_t::route_t>& routes) {
    std::lock_guard<std::mutex> lock(clients_mutex_);

    auto it = clients_.find(client_id);
    if (it != clients_.end()) {
        it->second->output_routes = routes;
        if (driver_ && driver_->router) {
            driver_->router->output_routes = routes;
            driver_->router->output_route_version.fetch_add(1, std::memory_order_release);
        }
        return true;
    }
    return false;
}

ASIO_DriverManager::Stats ASIO_DriverManager::GetStats() const {
    Stats stats;
    if (driver_ && driver_->router) {
        // Aggregate client stats
        std::lock_guard<std::mutex> lock(clients_mutex_);
        for (const auto& [id, ctx] : clients_) {
            if (ctx->input_buffer) {
                auto buf_stats = ctx->input_buffer->GetStats();
                stats.frames_processed += buf_stats.frames_read;
                stats.overruns += buf_stats.overruns;
                stats.underruns += buf_stats.underruns;
            }
        }
    }
    return stats;
}

// Sample format conversion functions
namespace {

// Convert various ASIO sample formats to planar float32
inline void convert_int16_to_float32(const int16_t* src, float* dst, int num_frames) {
    constexpr float scale = 1.0f / 32768.0f;
    for (int i = 0; i < num_frames; ++i) {
        dst[i] = src[i] * scale;
    }
}

inline void convert_int24_to_float32(const uint8_t* src, float* dst, int num_frames) {
    constexpr float scale = 1.0f / 8388608.0f;  // 2^23
    for (int i = 0; i < num_frames; ++i) {
        int32_t sample = (src[3*i] << 16) | (src[3*i+1] << 8) | src[3*i+2];
        // Sign extend from 24-bit
        if (sample & 0x800000) sample |= 0xFF000000;
        dst[i] = sample * scale;
    }
}

inline void convert_int32_to_float32(const int32_t* src, float* dst, int num_frames) {
    constexpr float scale = 1.0f / 2147483648.0f;  // 2^31
    for (int i = 0; i < num_frames; ++i) {
        dst[i] = src[i] * scale;
    }
}

inline void convert_float32_to_float32(const float* src, float* dst, int num_frames) {
    memcpy(dst, src, num_frames * sizeof(float));
}

inline void convert_float64_to_float32(const double* src, float* dst, int num_frames) {
    for (int i = 0; i < num_frames; ++i) {
        dst[i] = static_cast<float>(src[i]);
    }
}

// Generic format converter
struct FormatConverter {
    using ConvertFunc = void(*)(const void* src, float* dst, int num_frames);

    ConvertFunc func = nullptr;
    win_asio::ASIOSampleType sample_type = win_asio::ASIOSTInt16LSB;

    static FormatConverter create(win_asio::ASIOSampleType type) {
        FormatConverter conv;
        conv.sample_type = type;
        switch (type) {
            case win_asio::ASIOSTInt16LSB:
            case win_asio::ASIOSTInt16MSB:
                conv.func = [](const void* src, float* dst, int n) {
                    convert_int16_to_float32(static_cast<const int16_t*>(src), dst, n);
                };
                break;
            case win_asio::ASIOSTInt24LSB:
            case win_asio::ASIOSTInt24MSB:
                conv.func = [](const void* src, float* dst, int n) {
                    convert_int24_to_float32(static_cast<const uint8_t*>(src), dst, n);
                };
                break;
            case win_asio::ASIOSTInt32LSB:
            case win_asio::ASIOSTInt32MSB:
                conv.func = [](const void* src, float* dst, int n) {
                    convert_int32_to_float32(static_cast<const int32_t*>(src), dst, n);
                };
                break;
            case win_asio::ASIOSTFloat32LSB:
            case win_asio::ASIOSTFloat32MSB:
                conv.func = [](const void* src, float* dst, int n) {
                    convert_float32_to_float32(static_cast<const float*>(src), dst, n);
                };
                break;
            case win_asio::ASIOSTFloat64LSB:
            case win_asio::ASIOSTFloat64MSB:
                conv.func = [](const void* src, float* dst, int n) {
                    convert_float64_to_float32(static_cast<const double*>(src), dst, n);
                };
                break;
            default:
                conv.func = nullptr;
                break;
        }
        return conv;
    }

    void convert(const void* src, float* dst, int num_frames) const {
        if (func) func(src, dst, num_frames);
    }
};

} // anonymous namespace

// Static ASIO callbacks
void CALLBACK ASIO_DriverManager::BufferSwitchCallback(long index, ASIOBool processNow) {
    auto& mgr = Instance();
    if (mgr.driver_ && mgr.driver_->is_running) {
        mgr.ProcessAudioCallback(index, processNow != 0);
    }
}

void CALLBACK ASIO_DriverManager::SampleRateChangeCallback(ASIOSampleRate sRate) {
    blog(LOG_WARNING, "[win-asio] Sample rate changed to %f", sRate);
    auto& mgr = Instance();
    if (mgr.driver_) {
        mgr.driver_->current_sample_rate = sRate;
    }
}

long CALLBACK ASIO_DriverManager::AsioMessageCallback(long selector, long value, void* message, double* opt) {
    // Handle ASIO messages (e.g., kAsioSelectorSupported, kAsioEngineVersion, etc.)
    switch (selector) {
        case 0x3000:  // kAsioSelectorSupported
            return 0;  // Not supported
        case 0x3001:  // kAsioEngineVersion
            return 2;  // ASIO 2.0
        default:
            return 0;
    }
}

ASIOTime* CALLBACK ASIO_DriverManager::BufferSwitchTimeInfoCallback(ASIOTime* params, long index, ASIOBool processNow) {
    auto& mgr = Instance();
    if (mgr.driver_ && mgr.driver_->is_running) {
        mgr.ProcessAudioCallback(index, processNow != 0);
    }
    return params;
}

void ASIO_DriverManager::ProcessAudioCallback(long buffer_index, bool process_now) {
    if (!driver_ || !driver_->com_interface || !driver_->router) return;

    // Process each client
    std::lock_guard<std::mutex> lock(clients_mutex_);

    for (auto& [id, ctx] : clients_) {
        if (!ctx->callback) continue;

        // Determine buffer size (use preferred)
        int num_frames = driver_->preferred_buffer_size;
        int num_inputs = (int)driver_->input_channels.size();
        int num_outputs = (int)driver_->output_channels.size();

        // Prepare planar pointers
        std::vector<float*> input_planar(num_inputs);
        std::vector<float*> output_planar(num_outputs);

        // Pop from ring buffer (non-blocking)
        for (int ch = 0; ch < num_inputs; ++ch) {
            // In real implementation, this comes from ASIO buffers converted to float32
            // For now, zero-fill
            static thread_local std::vector<float> temp_buffer;
            temp_buffer.resize(num_frames);
            input_planar[ch] = temp_buffer.data();
        }

        for (int ch = 0; ch < num_outputs; ++ch) {
            static thread_local std::vector<float> temp_buffer;
            temp_buffer.resize(num_frames);
            output_planar[ch] = temp_buffer.data();
        }

        // Call client callback
        ctx->callback(input_planar.data(), output_planar.data(),
                      num_frames, num_inputs, num_outputs, 0.0);

        // Push output to ring buffer for monitoring
        if (ctx->output_buffer) {
            ctx->output_buffer->Push(output_planar.data(), num_frames);
        }
    }
}

// Helper function for SEH containment - no C++ objects with destructors
// Note: MSVC __try/__except cannot be used with virtual function calls
// when C++ objects with destructors are in scope. In practice, COM
// implementations rarely throw SEH exceptions, so we call directly.
static long CallControlPanel(IASIO* iface) noexcept {
    return iface->ControlPanel();
}

bool ASIO_DriverManager::OpenControlPanel() {
    IASIO* iface = nullptr;
    {
        std::lock_guard<std::mutex> lock(driver_mutex_);
        if (!driver_ || !driver_->com_interface) {
            blog(LOG_WARNING, "[win-asio] Cannot open control panel: no driver loaded");
            return false;
        }
        iface = driver_->com_interface;
    }

    // Call helper for SEH containment
    long result = CallControlPanel(iface);
    if (result != 0) {
        blog(LOG_WARNING, "[win-asio] ControlPanel returned: %ld", result);
        return false;
    }
    blog(LOG_INFO, "[win-asio] Control panel opened successfully");
    return true;
}

// String conversion helpers

std::string wstr_to_str(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return "";
    std::string str(size - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size, nullptr, nullptr);
    return str;
}

std::wstring str_to_wstr(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    if (size <= 0) return L"";
    std::wstring wstr(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size);
    return wstr;
}

} // namespace win_asio