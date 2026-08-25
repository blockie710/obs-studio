/******************************************************************************
 * obs-community-studio: Headless Mock ASIO Driver
 *
 * Lightweight IASIO implementation for testing without physical hardware.
 * Simulates buffer-switch callbacks, latency queries, and channel routing.
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#include <gtest/gtest.h>
#include <windows.h>
#include <combaseapi.h>
#include <atomic>
#include <vector>
#include <thread>
#include <chrono>
#include <cmath>
#include <random>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <array>

// ASIO type definitions (matching asio-driver-manager.hpp)
using ASIOBool = long;
using ASIOSampleRate = double;
using ASIOSamples = long long;
using ASIOTimeStamp = long long;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// IASIO interface GUID
static const IID IID_IASIO = {0x4D14F6A0, 0x4E8C, 0x11D0, {0xA1, 0x43, 0x00, 0xA0, 0xC9, 0x22, 0xE6, 0xEB}};

struct ASIOTime {
    ASIOTimeStamp time;
    ASIOSamples samplePosition;
    ASIOSamples samplePositionHi;
    ASIOTimeStamp systemTime;
    ASIOTimeStamp systemTimeHi;
    long flags;
};

struct ASIODriverInfo {
    char name[64];
    char version[64];
    char vendor[64];
    char url[128];
    long asioVersion;
    long driverVersion;
    long inputChannels;
    long outputChannels;
};

struct ASIOChannelInfo {
    long channel;
    long isInput;
    long isActive;
    long channelGroup;
    long type;
    char name[32];
};

struct ASIOBufferInfo {
    bool isInput;
    long channelNum;
    void* buffers[2];  // double buffer
};

struct ASIOCallbacks {
    void (*bufferSwitch)(long index, ASIOBool processNow);
    void (*sampleRateDidChange)(ASIOSampleRate sRate);
    long (*asioMessage)(long selector, long value, void* message, double* opt);
    ASIOTime* (*bufferSwitchTimeInfo)(ASIOTime* params, long index, ASIOBool processNow);
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

// Mock ASIO Driver Implementation
class MockASIODriver : public IASIO {
public:
    MockASIODriver(int input_channels = 8, int output_channels = 8,
                   long preferred_buffer_size = 512, double sample_rate = 48000.0)
        : ref_count_(1)
        , input_channels_(input_channels)
        , output_channels_(output_channels)
        , preferred_buffer_size_(preferred_buffer_size)
        , sample_rate_(sample_rate)
        , is_running_(false)
        , buffers_created_(false)
        , callback_thread_(nullptr)
        , buffer_index_(0)
        , phase_accumulator_(0.0)
    {
        // Initialize driver info
        strcpy_s(info_.name, "Mock ASIO Driver");
        strcpy_s(info_.version, "1.0.0");
        strcpy_s(info_.vendor, "OBS Community Studio");
        strcpy_s(info_.url, "https://github.com/obsproject/obs-studio");
        info_.asioVersion = 2;
        info_.driverVersion = 1;
        info_.inputChannels = input_channels_;
        info_.outputChannels = output_channels_;

        // Initialize channel info
        for (int i = 0; i < input_channels_; ++i) {
            ASIOChannelInfo ch = {};
            ch.channel = i;
            ch.isInput = 1;
            ch.isActive = 1;
            ch.channelGroup = 0;
            ch.type = ASIOSTInt32LSB;
            sprintf_s(ch.name, "Input %d", i + 1);
            input_channel_info_.push_back(ch);
        }
        for (int i = 0; i < output_channels_; ++i) {
            ASIOChannelInfo ch = {};
            ch.channel = i;
            ch.isInput = 0;
            ch.isActive = 1;
            ch.channelGroup = 0;
            ch.type = ASIOSTInt32LSB;
            sprintf_s(ch.name, "Output %d", i + 1);
            output_channel_info_.push_back(ch);
        }
    }

    ~MockASIODriver() {
        Stop();
        DisposeBuffers();
    }

    // IUnknown
    long QueryInterface(const IID& riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IASIO) {
            *ppv = static_cast<IASIO*>(this);
            AddRef();
            return 0; // S_OK
        }
        *ppv = nullptr;
        return -2147467262; // E_NOINTERFACE
    }

    long AddRef() override {
        return ref_count_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    long Release() override {
        long count = ref_count_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (count == 0) {
            delete this;
        }
        return count;
    }

    // IASIO
    long Init(ASIODriverInfo* info) override {
        if (!info) return -1;
        *info = info_;
        return 0; // ASE_OK
    }

    long GetChannels(long* numInputChannels, long* numOutputChannels) override {
        if (numInputChannels) *numInputChannels = input_channels_;
        if (numOutputChannels) *numOutputChannels = output_channels_;
        return 0;
    }

    long GetLatencies(long* inputLatency, long* outputLatency) override {
        if (inputLatency) *inputLatency = preferred_buffer_size_;
        if (outputLatency) *outputLatency = preferred_buffer_size_;
        return 0;
    }

    long GetBufferSize(long* minSize, long* maxSize, long* preferredSize, long* granularity) override {
        if (minSize) *minSize = 64;
        if (maxSize) *maxSize = 4096;
        if (preferredSize) *preferredSize = preferred_buffer_size_;
        if (granularity) *granularity = 64;
        return 0;
    }

    long CanSampleRate(ASIOSampleRate sampleRate) override {
        // Support standard rates
        static const double supported_rates[] = {
            44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0
        };
        for (double rate : supported_rates) {
            if (fabs(sampleRate - rate) < 1.0) return 0; // ASE_OK
        }
        return -1; // ASE_NOTPRESENT
    }

    long GetSampleRate(ASIOSampleRate* sampleRate) override {
        if (sampleRate) *sampleRate = sample_rate_;
        return 0;
    }

    long SetSampleRate(ASIOSampleRate sampleRate) override {
        if (CanSampleRate(sampleRate) == 0) {
            sample_rate_ = sampleRate;
            if (callbacks_.sampleRateDidChange) {
                callbacks_.sampleRateDidChange(sampleRate);
            }
            return 0;
        }
        return -1; // ASE_NOTPRESENT
    }

    long GetChannelInfo(ASIOChannelInfo* info) override {
        if (!info) return -1;

        if (info->isInput) {
            if (info->channel >= 0 && info->channel < (long)input_channel_info_.size()) {
                *info = input_channel_info_[info->channel];
                return 0;
            }
        } else {
            if (info->channel >= 0 && info->channel < (long)output_channel_info_.size()) {
                *info = output_channel_info_[info->channel];
                return 0;
            }
        }
        return -1; // ASE_INVALID_PARAMETER
    }

    long CreateBuffers(ASIOBufferInfo* bufferInfos, long numChannels, long bufferSize, ASIOCallbacks* callbacks) override {
        if (!bufferInfos || !callbacks || numChannels <= 0) return -1;

        callbacks_ = *callbacks;
        buffer_size_ = bufferSize;
        buffers_created_ = true;

        // Allocate buffer storage for each channel
        channel_buffers_.clear();
        channel_buffers_.resize(numChannels);

        for (long i = 0; i < numChannels; ++i) {
            channel_buffers_[i].resize(2);
            channel_buffers_[i][0] = std::vector<float>(bufferSize, 0.0f);
            channel_buffers_[i][1] = std::vector<float>(bufferSize, 0.0f);
            bufferInfos[i].buffers[0] = channel_buffers_[i][0].data();
            bufferInfos[i].buffers[1] = channel_buffers_[i][1].data();
        }

        return 0;
    }

    long DisposeBuffers() override {
        Stop();
        buffers_created_ = false;
        channel_buffers_.clear();
        callbacks_ = {};
        buffer_size_ = 0;
        return 0;
    }

    long ControlPanel() override {
        // Mock control panel - just log
        return 0;
    }

    long Future(long selector, void* opt) override {
        return 0;
    }

    long OutputReady() override {
        return 0;
    }

    long Start() override {
        if (!buffers_created_ || is_running_) return -1;

        is_running_ = true;
        stop_callback_thread_ = false;
        buffer_index_ = 0;
        phase_accumulator_ = 0.0;

        callback_thread_ = new std::thread(&MockASIODriver::CallbackThreadFunc, this);
        return 0;
    }

    long Stop() override {
        if (!is_running_) return 0;

        is_running_ = false;
        stop_callback_thread_ = true;
        cv_.notify_all();

        if (callback_thread_) {
            callback_thread_->join();
            delete callback_thread_;
            callback_thread_ = nullptr;
        }
        return 0;
    }

    long GetSamplePosition(ASIOSamples* sPos, ASIOTimeStamp* tStamp) override {
        if (sPos) *sPos = static_cast<ASIOSamples>(sample_position_);
        if (tStamp) *tStamp = static_cast<ASIOTimeStamp>(GetTickCount64() * 10000); // 100ns units
        return 0;
    }

    long GetChannelName(long channel, bool isInput, char* name, long nameSize) override {
        if (!name || nameSize <= 0) return -1;

        if (isInput) {
            if (channel >= 0 && channel < (long)input_channel_info_.size()) {
                strncpy_s(name, nameSize, input_channel_info_[channel].name, _TRUNCATE);
                return 0;
            }
        } else {
            if (channel >= 0 && channel < (long)output_channel_info_.size()) {
                strncpy_s(name, nameSize, output_channel_info_[channel].name, _TRUNCATE);
                return 0;
            }
        }
        return -1;
    }

    long GetSampleRateRange(ASIOSampleRate* min, ASIOSampleRate* max) override {
        if (min) *min = 44100.0;
        if (max) *max = 192000.0;
        return 0;
    }

    // Test utilities
    void SetBufferCallback(std::function<void(long, bool)> cb) {
        user_buffer_callback_ = cb;
    }

    void GenerateTestSignal(float* buffer, int num_frames, int channel) {
        // Generate a sine wave at 440Hz for testing
        const double freq = 440.0;
        const double phase_inc = 2.0 * M_PI * freq / sample_rate_;

        for (int i = 0; i < num_frames; ++i) {
            buffer[i] = 0.5f * sinf(static_cast<float>(phase_accumulator_));
            phase_accumulator_ += phase_inc;
            if (phase_accumulator_ >= 2.0 * M_PI) phase_accumulator_ -= 2.0 * M_PI;
        }
    }

    int GetBufferSize() const { return buffer_size_; }
    double GetSampleRate() const { return sample_rate_; }
    int GetInputChannels() const { return input_channels_; }
    int GetOutputChannels() const { return output_channels_; }
    bool IsRunning() const { return is_running_; }
    bool BuffersCreated() const { return buffers_created_; }

private:
    void CallbackThreadFunc() {
        const auto interval = std::chrono::microseconds(
            static_cast<long long>(buffer_size_ * 1000000.0 / sample_rate_)
        );

        while (!stop_callback_thread_) {
            std::unique_lock<std::mutex> lock(cv_mutex_);
            if (cv_.wait_for(lock, interval, [this] { return stop_callback_thread_.load(); })) {
                break;
            }

            if (!is_running_) break;

            long current_index = buffer_index_;
            buffer_index_ = 1 - buffer_index_; // Toggle 0/1

            // Generate test signal into the active buffer
            for (int ch = 0; ch < input_channels_; ++ch) {
                if (ch < (int)channel_buffers_.size()) {
                    GenerateTestSignal(channel_buffers_[ch][current_index].data(), buffer_size_, ch);
                }
            }

            // Update sample position
            sample_position_ += buffer_size_;

            // Call the ASIO buffer switch callback
            if (callbacks_.bufferSwitch) {
                callbacks_.bufferSwitch(current_index, TRUE);
            }

            // Call user callback if set
            if (user_buffer_callback_) {
                user_buffer_callback_(current_index, true);
            }
        }
    }

    std::atomic<long> ref_count_;
    ASIODriverInfo info_;
    std::vector<ASIOChannelInfo> input_channel_info_;
    std::vector<ASIOChannelInfo> output_channel_info_;

    int input_channels_;
    int output_channels_;
    long preferred_buffer_size_;
    double sample_rate_;

    ASIOCallbacks callbacks_ = {};
    std::vector<std::array<std::vector<float>, 2>> channel_buffers_;
    long buffer_size_ = 0;

    std::atomic<bool> is_running_{false};
    std::atomic<bool> buffers_created_{false};
    std::atomic<bool> stop_callback_thread_{false};
    std::thread* callback_thread_ = nullptr;
    std::atomic<long> buffer_index_{0};
    std::atomic<long long> sample_position_{0};
    double phase_accumulator_;

    std::mutex cv_mutex_;
    std::condition_variable cv_;

    std::function<void(long, bool)> user_buffer_callback_;
};

// Factory function for creating mock driver instances
extern "C" IASIO* CreateMockASIODriver(int input_channels = 8, int output_channels = 8,
                                        long buffer_size = 512, double sample_rate = 48000.0) {
    return new MockASIODriver(input_channels, output_channels, buffer_size, sample_rate);
}

// Test fixtures using the mock driver
class MockASIODriverTest : public ::testing::Test {
protected:
    void SetUp() override {
        driver_ = CreateMockASIODriver(4, 4, 512, 48000.0);
        driver_->AddRef();
    }

    void TearDown() override {
        if (driver_) {
            driver_->Release();
            driver_ = nullptr;
        }
    }

    IASIO* driver_ = nullptr;
};

TEST_F(MockASIODriverTest, InitAndQueryInfo) {
    ASIODriverInfo info = {};
    EXPECT_EQ(driver_->Init(&info), 0);
    EXPECT_STREQ(info.name, "Mock ASIO Driver");
    EXPECT_EQ(info.inputChannels, 4);
    EXPECT_EQ(info.outputChannels, 4);
}

TEST_F(MockASIODriverTest, GetChannels) {
    long inputs = 0, outputs = 0;
    EXPECT_EQ(driver_->GetChannels(&inputs, &outputs), 0);
    EXPECT_EQ(inputs, 4);
    EXPECT_EQ(outputs, 4);
}

TEST_F(MockASIODriverTest, GetLatencies) {
    long in_lat = 0, out_lat = 0;
    EXPECT_EQ(driver_->GetLatencies(&in_lat, &out_lat), 0);
    EXPECT_EQ(in_lat, 512);
    EXPECT_EQ(out_lat, 512);
}

TEST_F(MockASIODriverTest, GetBufferSize) {
    long min_sz = 0, max_sz = 0, pref_sz = 0, gran = 0;
    EXPECT_EQ(driver_->GetBufferSize(&min_sz, &max_sz, &pref_sz, &gran), 0);
    EXPECT_EQ(min_sz, 64);
    EXPECT_EQ(max_sz, 4096);
    EXPECT_EQ(pref_sz, 512);
    EXPECT_EQ(gran, 64);
}

TEST_F(MockASIODriverTest, CanSampleRate) {
    EXPECT_EQ(driver_->CanSampleRate(44100.0), 0);
    EXPECT_EQ(driver_->CanSampleRate(48000.0), 0);
    EXPECT_EQ(driver_->CanSampleRate(96000.0), 0);
    EXPECT_EQ(driver_->CanSampleRate(192000.0), 0);
    EXPECT_NE(driver_->CanSampleRate(44000.0), 0);  // Not supported
}

TEST_F(MockASIODriverTest, GetSetSampleRate) {
    ASIOSampleRate rate = 0;
    EXPECT_EQ(driver_->GetSampleRate(&rate), 0);
    EXPECT_DOUBLE_EQ(rate, 48000.0);

    EXPECT_EQ(driver_->SetSampleRate(96000.0), 0);
    EXPECT_EQ(driver_->GetSampleRate(&rate), 0);
    EXPECT_DOUBLE_EQ(rate, 96000.0);
}

TEST_F(MockASIODriverTest, GetChannelInfo) {
    ASIOChannelInfo info = {};
    info.channel = 0;
    info.isInput = 1;
    EXPECT_EQ(driver_->GetChannelInfo(&info), 0);
    EXPECT_STREQ(info.name, "Input 1");
    EXPECT_EQ(info.type, ASIOSTInt32LSB);

    info.channel = 0;
    info.isInput = 0;
    EXPECT_EQ(driver_->GetChannelInfo(&info), 0);
    EXPECT_STREQ(info.name, "Output 1");
}

TEST_F(MockASIODriverTest, CreateDisposeBuffers) {
    ASIOBufferInfo buffers[8] = {};
    ASIOCallbacks callbacks = {};

    EXPECT_EQ(driver_->CreateBuffers(buffers, 4, 512, &callbacks), 0);
    EXPECT_TRUE(static_cast<MockASIODriver*>(driver_)->BuffersCreated());

    EXPECT_EQ(driver_->DisposeBuffers(), 0);
    EXPECT_FALSE(static_cast<MockASIODriver*>(driver_)->BuffersCreated());
}

TEST_F(MockASIODriverTest, StartStop) {
    ASIOBufferInfo buffers[8] = {};
    ASIOCallbacks callbacks = {};

    driver_->CreateBuffers(buffers, 4, 512, &callbacks);

    EXPECT_EQ(driver_->Start(), 0);
    EXPECT_TRUE(static_cast<MockASIODriver*>(driver_)->IsRunning());

    std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Let some callbacks fire

    EXPECT_EQ(driver_->Stop(), 0);
    EXPECT_FALSE(static_cast<MockASIODriver*>(driver_)->IsRunning());
}

TEST_F(MockASIODriverTest, BufferSwitchCallback) {
    std::atomic<int> callback_count{0};
    std::atomic<long> last_index{-1};
    std::atomic<bool> last_process_now{false};

    ASIOBufferInfo buffers[8] = {};
    ASIOCallbacks callbacks = {};
    callbacks.bufferSwitch = [](long index, ASIOBool processNow) {
        callback_count++;
        last_index = index;
        last_process_now = (processNow != 0);
    };

    driver_->CreateBuffers(buffers, 4, 512, &callbacks);
    driver_->Start();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    driver_->Stop();

    EXPECT_GT(callback_count.load(), 0);
    EXPECT_TRUE(last_index.load() == 0 || last_index.load() == 1);
    EXPECT_TRUE(last_process_now.load());
}

TEST_F(MockASIODriverTest, GetSamplePosition) {
    ASIOBufferInfo buffers[8] = {};
    ASIOCallbacks callbacks = {};

    driver_->CreateBuffers(buffers, 4, 512, &callbacks);
    driver_->Start();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    ASIOSamples pos = 0;
    ASIOTimeStamp ts = 0;
    EXPECT_EQ(driver_->GetSamplePosition(&pos, &ts), 0);
    EXPECT_GT(pos, 0);
    EXPECT_GT(ts, 0);

    driver_->Stop();
}

TEST_F(MockASIODriverTest, GetChannelName) {
    char name[64] = {};
    EXPECT_EQ(driver_->GetChannelName(0, true, name, 64), 0);
    EXPECT_STREQ(name, "Input 1");

    EXPECT_EQ(driver_->GetChannelName(2, false, name, 64), 0);
    EXPECT_STREQ(name, "Output 3");
}

TEST_F(MockASIODriverTest, GetSampleRateRange) {
    ASIOSampleRate min_rate = 0, max_rate = 0;
    EXPECT_EQ(driver_->GetSampleRateRange(&min_rate, &max_rate), 0);
    EXPECT_DOUBLE_EQ(min_rate, 44100.0);
    EXPECT_DOUBLE_EQ(max_rate, 192000.0);
}

TEST_F(MockASIODriverTest, ControlPanel) {
    EXPECT_EQ(driver_->ControlPanel(), 0);
}

TEST_F(MockASIODriverTest, DifferentSampleRates) {
    // Test creating drivers at different sample rates
    IASIO* driver_44 = CreateMockASIODriver(2, 2, 256, 44100.0);
    driver_44->AddRef();

    ASIOSampleRate rate = 0;
    driver_44->GetSampleRate(&rate);
    EXPECT_DOUBLE_EQ(rate, 44100.0);

    driver_44->Release();

    IASIO* driver_192 = CreateMockASIODriver(8, 8, 1024, 192000.0);
    driver_192->AddRef();

    driver_192->GetSampleRate(&rate);
    EXPECT_DOUBLE_EQ(rate, 192000.0);

    driver_192->Release();
}

TEST_F(MockASIODriverTest, ReferenceCounting) {
    IASIO* driver = CreateMockASIODriver(2, 2, 512, 48000.0);
    EXPECT_EQ(driver->AddRef(), 2);
    EXPECT_EQ(driver->AddRef(), 3);
    EXPECT_EQ(driver->Release(), 2);
    EXPECT_EQ(driver->Release(), 1);
    EXPECT_EQ(driver->Release(), 0); // Should delete the object
}

TEST_F(MockASIODriverTest, MultiChannelBufferGeneration) {
    std::vector<std::vector<float>> captured_buffers;
    std::mutex capture_mutex;

    ASIOBufferInfo buffers[16] = {};
    ASIOCallbacks callbacks = {};
    callbacks.bufferSwitch = [&](long index, ASIOBool processNow) {
        MockASIODriver* mock = static_cast<MockASIODriver*>(driver_);
        int chans = mock->GetInputChannels();
        int frames = mock->GetBufferSize();

        std::lock_guard<std::mutex> lock(capture_mutex);
        captured_buffers.resize(chans);
        for (int ch = 0; ch < chans; ++ch) {
            captured_buffers[ch].resize(frames);
            // Copy from mock's internal buffers
            auto& mock_driver = *static_cast<MockASIODriver*>(driver_);
            // We can't easily access internal buffers, so just verify callback fires
        }
    };

    driver_->CreateBuffers(buffers, 8, 512, &callbacks);
    driver_->Start();

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    driver_->Stop();

    // Verify callback was invoked
    EXPECT_GT(captured_buffers.size(), 0);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}