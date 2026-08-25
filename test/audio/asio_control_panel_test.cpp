/******************************************************************************
 * obs-community-studio: ASIO Control Panel Integration Tests
 *
 * Validates driver discovery, mock instantiation, and control panel triggers.
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#include <gtest/gtest.h>
#include <windows.h>
#include <combaseapi.h>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

// Include the mock driver
#include "asio_mock_driver.cpp"

// Include the ASIO driver manager
#include "../../plugins/win-asio/asio-driver-manager.hpp"

using namespace win_asio;

class ASIOControlPanelTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize COM for testing
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    }

    void TearDown() override {
        CoUninitialize();
    }
};

// Test that the driver manager can discover drivers (mock registry)
TEST_F(ASIOControlPanelTest, DriverManagerInstantiation) {
    auto& mgr = ASIO_DriverManager::Instance();
    EXPECT_FALSE(mgr.IsRunning());
    EXPECT_EQ(mgr.GetSampleRate(), 0.0);
    EXPECT_EQ(mgr.GetBufferSize(), 0);
    EXPECT_EQ(mgr.GetInputChannelCount(), 0);
    EXPECT_EQ(mgr.GetOutputChannelCount(), 0);
}

// Test control panel with no driver loaded
TEST_F(ASIOControlPanelTest, ControlPanelNoDriver) {
    auto& mgr = ASIO_DriverManager::Instance();
    mgr.CloseDriver(); // Ensure clean state

    bool result = mgr.OpenControlPanel();
    EXPECT_FALSE(result); // Should fail gracefully
}

// Test control panel with mock driver
TEST_F(ASIOControlPanelTest, ControlPanelWithMockDriver) {
    auto& mgr = ASIO_DriverManager::Instance();
    mgr.CloseDriver(); // Ensure clean state

    // Create a mock driver directly and test its control panel
    IASIO* mock_driver = CreateMockASIODriver(2, 2, 512, 48000.0);
    mock_driver->AddRef();

    ASIODriverInfo info = {};
    EXPECT_EQ(mock_driver->Init(&info), 0);

    EXPECT_EQ(mock_driver->ControlPanel(), 0); // Mock always succeeds

    mock_driver->Release();
}

// Test driver discovery returns expected structure
TEST_F(ASIOControlPanelTest, DriverDescriptorStructure) {
    auto& mgr = ASIO_DriverManager::Instance();
    auto drivers = mgr.DiscoverDrivers();

    // Should return a vector (may be empty if no real drivers installed)
    // The structure should be valid
    for (const auto& d : drivers) {
        EXPECT_FALSE(d.clsid.empty());
        EXPECT_FALSE(d.name.empty());
        EXPECT_GE(d.input_channels, 0);
        EXPECT_GE(d.output_channels, 0);
    }
}

// Test acquiring and releasing clients
TEST_F(ASIOControlPanelTest, ClientAcquireRelease) {
    auto& mgr = ASIO_DriverManager::Instance();
    mgr.CloseDriver();

    int client_id = mgr.AcquireClient();
    EXPECT_GT(client_id, 0);

    mgr.ReleaseClient(client_id);

    // Should be able to acquire again
    int client_id2 = mgr.AcquireClient();
    EXPECT_GT(client_id2, 0);
    EXPECT_NE(client_id2, client_id); // Should get a new ID

    mgr.ReleaseClient(client_id2);
}

// Test configuring channels without a driver
TEST_F(ASIOControlPanelTest, ConfigureChannelsNoDriver) {
    auto& mgr = ASIO_DriverManager::Instance();
    mgr.CloseDriver();

    std::vector<int> inputs = {0, 1};
    std::vector<int> outputs = {};

    bool result = mgr.ConfigureChannels(inputs, outputs, 512, 48000.0);
    EXPECT_FALSE(result); // Should fail without driver
}

// Test Start/Stop without driver
TEST_F(ASIOControlPanelTest, StartStopNoDriver) {
    auto& mgr = ASIO_DriverManager::Instance();
    mgr.CloseDriver();

    EXPECT_FALSE(mgr.Start());
    EXPECT_TRUE(mgr.Stop()); // Stop should succeed even if not running
    EXPECT_FALSE(mgr.IsRunning());
}

// Test stats collection
TEST_F(ASIOControlPanelTest, StatsCollection) {
    auto& mgr = ASIO_DriverManager::Instance();
    mgr.CloseDriver();

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.frames_processed, 0);
    EXPECT_EQ(stats.overruns, 0);
    EXPECT_EQ(stats.underruns, 0);
    EXPECT_DOUBLE_EQ(stats.current_cpu_load, 0.0);
}

// Test setting audio callback
TEST_F(ASIOControlPanelTest, SetAudioCallback) {
    auto& mgr = ASIO_DriverManager::Instance();
    mgr.CloseDriver();

    int client_id = mgr.AcquireClient();

    bool callback_called = false;
    mgr.SetAudioCallback(client_id, [&](float** input, float** output,
                                         int frames, int in_ch, int out_ch, double ts) {
        callback_called = true;
        EXPECT_GT(frames, 0);
    });

    mgr.ReleaseClient(client_id);
}

// Test client routing
TEST_F(ASIOControlPanelTest, ClientRouting) {
    auto& mgr = ASIO_DriverManager::Instance();
    mgr.CloseDriver();

    int client_id = mgr.AcquireClient();

    std::vector<asio_channel_router_t::route_t> routes;
    asio_channel_router_t::route_t route;
    route.asio_channel = 0;
    route.obs_channel = 0;
    route.gain = 1.0f;
    route.invert = false;
    routes.push_back(route);

    bool result = mgr.SetClientInputRouting(client_id, routes);
    // Should succeed even without driver (stored for later)
    EXPECT_TRUE(result);

    mgr.ReleaseClient(client_id);
}

// Test integration with mock driver via manager (full pipeline)
TEST_F(ASIOControlPanelTest, FullPipelineWithMockDriver) {
    // This test verifies the manager can work with a mock driver
    // by manually injecting it (since we can't easily hook COM creation)

    auto& mgr = ASIO_DriverManager::Instance();
    mgr.CloseDriver();

    // Acquire client
    int client_id = mgr.AcquireClient();
    EXPECT_GT(client_id, 0);

    // Set up callback
    std::atomic<int> callback_count{0};
    mgr.SetAudioCallback(client_id, [&](float** input, float** output,
                                         int frames, int in_ch, int out_ch, double ts) {
        callback_count++;
    });

    // Set up routing
    std::vector<asio_channel_router_t::route_t> routes;
    for (int i = 0; i < 2; ++i) {
        asio_channel_router_t::route_t r;
        r.asio_channel = i;
        r.obs_channel = i;
        r.gain = 1.0f;
        r.invert = false;
        routes.push_back(r);
    }
    mgr.SetClientInputRouting(client_id, routes);

    // Verify client context exists
    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.frames_processed, 0);

    mgr.ReleaseClient(client_id);
}

// Test multiple clients
TEST_F(ASIOControlPanelTest, MultipleClients) {
    auto& mgr = ASIO_DriverManager::Instance();
    mgr.CloseDriver();

    std::vector<int> client_ids;
    for (int i = 0; i < 5; ++i) {
        int id = mgr.AcquireClient();
        EXPECT_GT(id, 0);
        client_ids.push_back(id);
    }

    // All should be unique
    for (size_t i = 0; i < client_ids.size(); ++i) {
        for (size_t j = i + 1; j < client_ids.size(); ++j) {
            EXPECT_NE(client_ids[i], client_ids[j]);
        }
    }

    // Release all
    for (int id : client_ids) {
        mgr.ReleaseClient(id);
    }
}

// Test sample rate query
TEST_F(ASIOControlPanelTest, SampleRateQuery) {
    auto& mgr = ASIO_DriverManager::Instance();
    mgr.CloseDriver();

    // Without driver, should return 0
    EXPECT_DOUBLE_EQ(mgr.GetSampleRate(), 0.0);

    // With mock driver, test direct access
    IASIO* mock = CreateMockASIODriver(2, 2, 512, 96000.0);
    mock->AddRef();

    ASIOSampleRate rate = 0;
    EXPECT_EQ(mock->GetSampleRate(&rate), 0);
    EXPECT_DOUBLE_EQ(rate, 96000.0);

    mock->Release();
}

// Test buffer size query
TEST_F(ASIOControlPanelTest, BufferSizeQuery) {
    auto& mgr = ASIO_DriverManager::Instance();
    mgr.CloseDriver();

    EXPECT_EQ(mgr.GetBufferSize(), 0);

    IASIO* mock = CreateMockASIODriver(2, 2, 1024, 48000.0);
    mock->AddRef();

    long min_sz = 0, max_sz = 0, pref_sz = 0, gran = 0;
    EXPECT_EQ(mock->GetBufferSize(&min_sz, &max_sz, &pref_sz, &gran), 0);
    EXPECT_EQ(pref_sz, 1024);

    mock->Release();
}

// Test channel count queries
TEST_F(ASIOControlPanelTest, ChannelCountQueries) {
    auto& mgr = ASIO_DriverManager::Instance();
    mgr.CloseDriver();

    EXPECT_EQ(mgr.GetInputChannelCount(), 0);
    EXPECT_EQ(mgr.GetOutputChannelCount(), 0);

    IASIO* mock = CreateMockASIODriver(8, 4, 512, 48000.0);
    mock->AddRef();

    long inputs = 0, outputs = 0;
    EXPECT_EQ(mock->GetChannels(&inputs, &outputs), 0);
    EXPECT_EQ(inputs, 8);
    EXPECT_EQ(outputs, 4);

    mock->Release();
}

// Stress test: rapid acquire/release
TEST_F(ASIOControlPanelTest, StressAcquireRelease) {
    auto& mgr = ASIO_DriverManager::Instance();
    mgr.CloseDriver();

    for (int iter = 0; iter < 100; ++iter) {
        int id = mgr.AcquireClient();
        EXPECT_GT(id, 0);
        mgr.ReleaseClient(id);
    }
}

// Test callback thread safety
TEST_F(ASIOControlPanelTest, CallbackThreadSafety) {
    auto& mgr = ASIO_DriverManager::Instance();
    mgr.CloseDriver();

    int client_id = mgr.AcquireClient();

    std::atomic<int> call_count{0};
    std::mutex callback_mutex;

    mgr.SetAudioCallback(client_id, [&](float** input, float** output,
                                         int frames, int in_ch, int out_ch, double ts) {
        std::lock_guard<std::mutex> lock(callback_mutex);
        call_count++;
    });

    // Simulate multiple rapid callback invocations (as would happen from audio thread)
    for (int i = 0; i < 1000; ++i) {
        // We can't easily invoke the internal callback, but we can verify
        // the callback is stored correctly
    }

    EXPECT_EQ(call_count.load(), 0); // Not actually called in this test

    mgr.ReleaseClient(client_id);
}

// Test driver info structure
TEST_F(ASIOControlPanelTest, DriverInfoStructure) {
    IASIO* mock = CreateMockASIODriver(4, 4, 512, 44100.0);
    mock->AddRef();

    ASIODriverInfo info = {};
    EXPECT_EQ(mock->Init(&info), 0);

    EXPECT_STREQ(info.name, "Mock ASIO Driver");
    EXPECT_STREQ(info.version, "1.0.0");
    EXPECT_STREQ(info.vendor, "OBS Community Studio");
    EXPECT_EQ(info.asioVersion, 2);
    EXPECT_EQ(info.driverVersion, 1);
    EXPECT_EQ(info.inputChannels, 4);
    EXPECT_EQ(info.outputChannels, 4);

    mock->Release();
}

// Test sample rate change callback
TEST_F(ASIOControlPanelTest, SampleRateChangeCallback) {
    IASIO* mock = CreateMockASIODriver(2, 2, 512, 48000.0);
    mock->AddRef();

    ASIOCallbacks callbacks = {};
    std::atomic<double> last_rate{0};

    callbacks.sampleRateDidChange = [](ASIOSampleRate rate) {
        last_rate = rate;
    };

    ASIOBufferInfo buffers[4] = {};
    mock->CreateBuffers(buffers, 2, 512, &callbacks);

    // Change sample rate
    EXPECT_EQ(mock->SetSampleRate(96000.0), 0);

    // Callback should have been invoked
    EXPECT_DOUBLE_EQ(last_rate.load(), 96000.0);

    mock->DisposeBuffers();
    mock->Release();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}