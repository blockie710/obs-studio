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

#include <obs-module.h>
#include <obs.h>

// Forward declarations for ASIO SDK types (avoid including asio.h in header)
struct ASIOCallbacks;
struct ASIOTime;
typedef int32_t ASIOBool;
typedef double ASIOSampleRate;

#ifdef _WIN32
#define ASIO_CALLING_CONVENTION __stdcall
#else
#define ASIO_CALLING_CONVENTION
#endif

#include "asio-manager.h"
#include "asio-ringbuffer.h"
#include "asio-resampler.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct ASIOSource {
    obs_source_t* source;
    
    // ASIO device
    std::shared_ptr<win_asio::DeviceHandle> deviceHandle;
    CLSID deviceCLSID = GUID_NULL;
    std::wstring deviceName;
    
    // Channel selection
    std::vector<int> activeChannels;
    int numActiveChannels = 0;
    
    // Audio settings
    int bufferSize = 0;
    int32_t sampleRate = 0;
    bool useDeviceTiming = true;
    
    // Ring buffer for audio data (per channel)
    static constexpr size_t RingBufferCapacity = 8192;
    using AudioRingBuffer = win_asio::RingBuffer<float, 8192>;
    std::unique_ptr<win_asio::RingBuffer<float, 8192>[]> channelBuffers;
    
    // Resampler
    win_asio::ResamplerContext resampler;
    int obsSampleRate = 48000;
    
    // Threading
    std::thread consumerThread;
    std::atomic<bool> running{false};
    std::atomic<bool> stopRequested{false};
    
    // Timing
    int64_t lastTimestamp = 0;
    int64_t samplesProcessed = 0;
    
    // Audio output info
    struct obs_audio_data outputAudio;
    std::vector<float> outputBuffer;
    
    ASIOSource(obs_source_t* source_);
    ~ASIOSource();
    
    bool initialize();
    void shutdown();
    void updateSettings(obs_data_t* settings);
    void startConsumerThread();
    void stopConsumerThread();
    void consumeAudio();
    bool processAudio(float** output, int maxSamples);
    
    // Real-time callback thunks (called from driver thread)
    static void ASIO_CALLING_CONVENTION bufferSwitchThunk(int32_t doubleBufferIndex, ASIOBool directProcess);
    static ASIOTime* ASIO_CALLING_CONVENTION bufferSwitchTimeInfoThunk(ASIOTime* params,
                                                                       int32_t doubleBufferIndex,
                                                                       ASIOBool directProcess);
    static void ASIO_CALLING_CONVENTION sampleRateDidChangeThunk(ASIOSampleRate sRate);
    static int32_t ASIO_CALLING_CONVENTION asioMessageThunk(int32_t selector, int32_t value,
                                                              void* message, double* opt);
};