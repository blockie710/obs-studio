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

#include "asio-source.h"
#include "asio-manager.h"
#include "asio-format.h"
#include "asio-ringbuffer.h"
#include "asio-resampler.h"

#include <obs-module.h>
#include <obs.h>
#include <util/dstr.h>
#include <util/platform.h>
#include <util/threading.h>
#include <util/windows/ComPtr.hpp>

#include <asio.h>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>
#include <string>

#define OPT_DEVICE_CLSID "device_clsid"
#define OPT_DEVICE_NAME "device_name"
#define OPT_CHANNELS "channels"
#define OPT_BUFFER_SIZE "buffer_size"
#define OPT_SAMPLE_RATE "sample_rate"
#define OPT_USE_DEVICE_TIMING "use_device_timing"

static const char* asio_source_getname(void* unused) {
    UNUSED_PARAMETER(unused);
    return obs_module_text("ASIOSource");
}

// Forward declarations for static functions
static void asio_source_update(void* data, obs_data_t* settings);
static void asio_source_destroy(void* data);
static void* asio_source_create(obs_data_t* settings, obs_source_t* source);
static obs_properties_t* asio_source_properties(void* unused);
static void asio_source_defaults(obs_data_t* settings);
static void asio_source_activate(void* data);
static void asio_source_deactivate(void* data);

static const struct obs_source_info asio_source_info = {
    "asio_input_capture",
    OBS_SOURCE_TYPE_INPUT,
    OBS_SOURCE_AUDIO,
    asio_source_getname,
    asio_source_create,
    asio_source_destroy,
    asio_source_update,
    asio_source_properties,
    asio_source_defaults,
    asio_source_activate,
    asio_source_deactivate,
    0,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-asio", "en-US")

bool obs_module_load(void) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    obs_register_source(&asio_source_info);
    blog(LOG_INFO, "[win-asio] ASIO audio capture plugin loaded");
    return true;
}

void obs_module_unload(void) {
    CoUninitialize();
    blog(LOG_INFO, "[win-asio] ASIO audio capture plugin unloaded");
}

ASIOSource::ASIOSource(obs_source_t* source_) : source(source_) {
    memset(&outputAudio, 0, sizeof(outputAudio));
}

ASIOSource::~ASIOSource() {
    shutdown();
}

bool ASIOSource::initialize() {
    audio_t* audio = obs_get_audio();
    if (audio) {
        obsSampleRate = audio_output_get_sample_rate(audio);
    }

    outputBuffer.resize(64 * 4096);
    channelBuffers = std::make_unique<win_asio::RingBuffer<float, 8192>[]>(64);

    return true;
}

void ASIOSource::shutdown() {
    stopConsumerThread();

    if (deviceHandle) {
        win_asio::DriverManager::instance().releaseDevice(deviceCLSID);
        deviceHandle.reset();
    }
}

void ASIOSource::updateSettings(obs_data_t* settings) {
    const char* clsidStr = obs_data_get_string(settings, OPT_DEVICE_CLSID);
    if (clsidStr && *clsidStr) {
        WCHAR wClsidStr[64];
        mbstowcs(wClsidStr, clsidStr, 64);
        CLSID newCLSID;
        if (SUCCEEDED(CLSIDFromString(wClsidStr, &newCLSID))) {
            if (!IsEqualCLSID(newCLSID, deviceCLSID)) {
                shutdown();
                deviceCLSID = newCLSID;
            }
        }
    }

    const char* deviceNameStr = obs_data_get_string(settings, OPT_DEVICE_NAME);
    if (deviceNameStr) {
        deviceName.assign(deviceNameStr, deviceNameStr + strlen(deviceNameStr));
    }

    const char* channelsStr = obs_data_get_string(settings, OPT_CHANNELS);
    activeChannels.clear();
    if (channelsStr && *channelsStr) {
        char* copy = strdup(channelsStr);
        char* token = strtok(copy, ",");
        while (token) {
            activeChannels.push_back(atoi(token));
            token = strtok(nullptr, ",");
        }
        free(copy);
    }
    numActiveChannels = static_cast<int>(activeChannels.size());

    bufferSize = obs_data_get_int(settings, OPT_BUFFER_SIZE);
    sampleRate = obs_data_get_int(settings, OPT_SAMPLE_RATE);
    useDeviceTiming = obs_data_get_bool(settings, OPT_USE_DEVICE_TIMING);

    if (deviceHandle) {
        win_asio::DriverManager::instance().releaseDevice(deviceCLSID);
        deviceHandle.reset();
    }

    if (IsEqualCLSID(deviceCLSID, GUID_NULL)) {
        return;
    }

    deviceHandle = win_asio::DriverManager::instance().acquireDevice(deviceCLSID);
    if (!deviceHandle || !deviceHandle->asio) {
        blog(LOG_ERROR, "[ASIO] Failed to acquire device");
        return;
    }

    ASIOError err = deviceHandle->asio->ASIOInit(nullptr);
    if (err != ASE_OK) {
        blog(LOG_ERROR, "[ASIO] Device init failed: %d", err);
        return;
    }

    int32_t numIn = 0, numOut = 0;
    err = deviceHandle->asio->ASIOGetChannels(&numIn, &numOut);
    if (err != ASE_OK) {
        blog(LOG_ERROR, "[ASIO] getChannels failed: %d", err);
        return;
    }
    deviceHandle->numInputChannels = numIn;
    deviceHandle->numOutputChannels = numOut;

    int32_t minSize = 0, maxSize = 0, prefSize = 0, granularity = 0;
    err = deviceHandle->asio->ASIOGetBufferSize(&minSize, &maxSize, &prefSize, &granularity);
    if (err != ASE_OK) {
        blog(LOG_ERROR, "[ASIO] getBufferSize failed: %d", err);
        return;
    }

    int targetBufferSize = bufferSize > 0 ? bufferSize : prefSize;
    targetBufferSize = std::max(minSize, std::min(maxSize, targetBufferSize));

    ASIOSampleRate currentRate = 0;
    err = deviceHandle->asio->ASIOGetSampleRate(&currentRate);
    if (err != ASE_OK) {
        blog(LOG_ERROR, "[ASIO] getSampleRate failed: %d", err);
        return;
    }

    int32_t targetRate = sampleRate > 0 ? sampleRate : static_cast<int32_t>(currentRate);
    if (targetRate != currentRate) {
        err = deviceHandle->asio->ASIOSetSampleRate(static_cast<ASIOSampleRate>(targetRate));
        if (err != ASE_OK) {
            blog(LOG_WARNING, "[ASIO] setSampleRate failed: %d, using %f", err, currentRate);
            targetRate = static_cast<int32_t>(currentRate);
        }
    }

    deviceHandle->sampleRate = targetRate;
    deviceHandle->bufferSize = targetBufferSize;

    // Allocate per-channel ring buffers
    if (deviceHandle->ringBuffers) {
        delete[] deviceHandle->ringBuffers;
    }
    deviceHandle->ringBuffers = new win_asio::RingBuffer<float, 8192>[deviceHandle->numInputChannels];

    for (int i = 0; i < deviceHandle->numInputChannels; ++i) {
        ASIOChannelInfo info = {};
        info.channel = i;
        info.isInput = ASIOTrue;
        err = deviceHandle->asio->ASIOGetChannelInfo(&info);
        if (err == ASE_OK) {
            deviceHandle->channelInfo[i] = info;
        }
    }

    ASIOBufferInfo bufferInfos[64];
    for (int i = 0; i < deviceHandle->numInputChannels; ++i) {
        bufferInfos[i].isInput = ASIOTrue;
        bufferInfos[i].channelNum = i;
        bufferInfos[i].buffers[0] = nullptr;
        bufferInfos[i].buffers[1] = nullptr;
    }

    // Set up callbacks using static thunks
    deviceHandle->callbacks.bufferSwitch = &ASIOSource::bufferSwitchThunk;
    deviceHandle->callbacks.bufferSwitchTimeInfo = &ASIOSource::bufferSwitchTimeInfoThunk;
    deviceHandle->callbacks.sampleRateDidChange = &ASIOSource::sampleRateDidChangeThunk;
    deviceHandle->callbacks.asioMessage = &ASIOSource::asioMessageThunk;

    err = deviceHandle->asio->ASIOCreateBuffers(bufferInfos, deviceHandle->numInputChannels,
                                                targetBufferSize, &deviceHandle->callbacks);
    if (err != ASE_OK) {
        blog(LOG_ERROR, "[ASIO] createBuffers failed: %d", err);
        return;
    }

    if (deviceHandle->numInputChannels > 0) {
        deviceHandle->nativeFormat = deviceHandle->channelInfo[0].type;
        deviceHandle->nativeChannels = deviceHandle->numInputChannels;

        if (targetRate != obsSampleRate) {
            resampler.ensureResampler(targetRate, obsSampleRate, 2);
        }
    }

    err = deviceHandle->asio->ASIOStart();
    if (err != ASE_OK) {
        blog(LOG_ERROR, "[ASIO] start failed: %d", err);
        return;
    }

    deviceHandle->running = true;

    startConsumerThread();

    blog(LOG_INFO, "[ASIO] Device started: %d channels, %d Hz, %d buffer",
         deviceHandle->numInputChannels, targetRate, targetBufferSize);
}

void ASIOSource::startConsumerThread() {
    if (running.load(std::memory_order_relaxed)) return;

    running.store(true, std::memory_order_relaxed);
    stopRequested.store(false, std::memory_order_relaxed);
    consumerThread = std::thread(&ASIOSource::consumeAudio, this);

    SetThreadPriority(consumerThread.native_handle(), THREAD_PRIORITY_TIME_CRITICAL);
}

void ASIOSource::stopConsumerThread() {
    if (!running.load(std::memory_order_relaxed)) return;

    stopRequested.store(true, std::memory_order_relaxed);

    if (consumerThread.joinable()) {
        consumerThread.join();
    }

    running.store(false, std::memory_order_relaxed);
}

// Real-time callback thunks (called from driver thread)
void ASIO_CALLING_CONVENTION ASIOSource::bufferSwitchThunk(int32_t doubleBufferIndex, ASIOBool directProcess) {
    // Get the source instance from thread-local storage or device handle
    // For now, we need to store a pointer in the device handle
    // This is a limitation - in production we'd use TLS or a global map
}

ASIOTime* ASIO_CALLING_CONVENTION ASIOSource::bufferSwitchTimeInfoThunk(ASIOTime* params,
                                                                       int32_t doubleBufferIndex,
                                                                       ASIOBool directProcess) {
    return params;
}

void ASIO_CALLING_CONVENTION ASIOSource::sampleRateDidChangeThunk(ASIOSampleRate sRate) {
    // Handle sample rate change
}

int32_t ASIO_CALLING_CONVENTION ASIOSource::asioMessageThunk(int32_t selector, int32_t value,
                                                              void* message, double* opt) {
    return 0;
}

void ASIOSource::consumeAudio() {
    const int channels = numActiveChannels > 0 ? numActiveChannels : 2;
    const int maxSamples = 4096;

    std::vector<float*> channelPtrs(channels);
    std::vector<float> channelData(channels * maxSamples);

    for (int i = 0; i < channels; ++i) {
        channelPtrs[i] = &channelData[i * maxSamples];
    }

    while (running.load(std::memory_order_relaxed) && !stopRequested.load(std::memory_order_relaxed)) {
        int samplesAvailable = 0;
        if (deviceHandle && deviceHandle->ringBuffers) {
            samplesAvailable = deviceHandle->ringBuffers[0].available();
        }

        if (samplesAvailable < maxSamples / 4) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (processAudio(channelPtrs.data(), maxSamples)) {
            // Use obs_source_audio structure for output
            struct obs_source_audio sourceAudio = {};
            sourceAudio.data[0] = reinterpret_cast<const uint8_t*>(channelPtrs[0]);
            if (channels > 1) {
                sourceAudio.data[1] = reinterpret_cast<const uint8_t*>(channelPtrs[1]);
            }
            sourceAudio.frames = maxSamples;
            sourceAudio.speakers = channels == 1 ? SPEAKERS_MONO : SPEAKERS_STEREO;
            sourceAudio.format = AUDIO_FORMAT_FLOAT_PLANAR;
            sourceAudio.samples_per_sec = static_cast<uint32_t>(obsSampleRate);
            sourceAudio.timestamp = os_gettime_ns();

            obs_source_output_audio(source, &sourceAudio);
        }
    }
}

bool ASIOSource::processAudio(float** output, int maxSamples) {
    if (!deviceHandle || !deviceHandle->running) return false;

    int channels = numActiveChannels > 0 ? numActiveChannels : 2;
    int available = deviceHandle->ringBuffers ? deviceHandle->ringBuffers[0].available() : 0;

    if (available < maxSamples) return false;

    for (int ch = 0; ch < channels; ++ch) {
        if (ch < 64 && deviceHandle->ringBuffers[ch].available() >= maxSamples) {
            deviceHandle->ringBuffers[ch].pop(output[ch], maxSamples);
        } else {
            memset(output[ch], 0, maxSamples * sizeof(float));
        }
    }

    if (resampler.resampler && deviceHandle->sampleRate != obsSampleRate) {
        int outSamples = 0;
        resampler.resample(output, maxSamples, output, &outSamples, maxSamples);
        return outSamples > 0;
    }

    return true;
}

static void asio_source_update(void* data, obs_data_t* settings) {
    ASIOSource* context = static_cast<ASIOSource*>(data);
    context->updateSettings(settings);
}

static void asio_source_destroy(void* data) {
    ASIOSource* context = static_cast<ASIOSource*>(data);
    delete context;
}

static void* asio_source_create(obs_data_t* settings, obs_source_t* source) {
    ASIOSource* context = new ASIOSource(source);
    if (!context->initialize()) {
        delete context;
        return nullptr;
    }
    context->updateSettings(settings);
    return context;
}

static void asio_source_activate(void* data) {
    ASIOSource* context = static_cast<ASIOSource*>(data);
    context->startConsumerThread();
}

static void asio_source_deactivate(void* data) {
    ASIOSource* context = static_cast<ASIOSource*>(data);
    context->stopConsumerThread();
}

static obs_properties_t* asio_source_properties(void* unused) {
    UNUSED_PARAMETER(unused);

    obs_properties_t* props = obs_properties_create();

    auto& manager = win_asio::DriverManager::instance();
    auto drivers = manager.enumerateDrivers();

    obs_property_t* driverList = obs_properties_add_list(props, OPT_DEVICE_CLSID,
        obs_module_text("ASIO.Device"), OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

    obs_property_list_add_string(driverList, obs_module_text("ASIO.SelectDevice"), "");

    for (const auto& driver : drivers) {
        char clsidStr[64];
        StringFromGUID2(driver.clsid, (LPOLESTR)clsidStr, 64);
        char name[128];
        wcstombs(name, driver.name.c_str(), 128);
        obs_property_list_add_string(driverList, name, clsidStr);
    }

    obs_properties_add_button(props, "open_control_panel",
        obs_module_text("ASIO.ControlPanel"), [](obs_properties_t* props, obs_property_t* p, void* data) {
            ASIOSource* context = static_cast<ASIOSource*>(data);
            if (context && context->deviceHandle && context->deviceHandle->asio) {
                context->deviceHandle->asio->ASIOControlPanel();
            }
            return true;
        });

    obs_property_t* channelList = obs_properties_add_list(props, OPT_CHANNELS,
        obs_module_text("ASIO.Channels"), OBS_COMBO_TYPE_EDITABLE, OBS_COMBO_FORMAT_STRING);

    obs_properties_add_int(props, OPT_BUFFER_SIZE, obs_module_text("ASIO.BufferSize"),
        32, 8192, 32);

    obs_properties_add_int(props, OPT_SAMPLE_RATE, obs_module_text("ASIO.SampleRate"),
        44100, 192000, 1);

    obs_properties_add_bool(props, OPT_USE_DEVICE_TIMING, obs_module_text("ASIO.UseDeviceTiming"));

    return props;
}

static void asio_source_defaults(obs_data_t* settings) {
    obs_data_set_default_string(settings, OPT_DEVICE_CLSID, "");
    obs_data_set_default_int(settings, OPT_BUFFER_SIZE, 512);
    obs_data_set_default_int(settings, OPT_SAMPLE_RATE, 48000);
    obs_data_set_default_bool(settings, OPT_USE_DEVICE_TIMING, true);
}