/******************************************************************************
 * obs-community-studio: VST3 State Serialization Unit Tests
 *
 * Tests for VST3 plugin state serialization/deserialization
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <cstring>

#include "vst3-plugin.hpp"
#include "vst3-controller.hpp"

using namespace obs_vst3;

class VST3StateTest : public ::testing::Test {
protected:
    void SetUp() override {
        plugin = std::make_unique<VST3Plugin>();
    }

    void TearDown() override {
        plugin.reset();
    }

    std::unique_ptr<VST3Plugin> plugin;
};

TEST_F(VST3StateTest, StateVectorStructure) {
    // Test that state serialization uses vector<uint8_t>
    std::vector<uint8_t> state;
    state.resize(1024);

    // Fill with test pattern
    for (size_t i = 0; i < state.size(); ++i) {
        state[i] = static_cast<uint8_t>(i & 0xFF);
    }

    bool result = plugin->getState(state);
    // Without SDK, this returns false - but interface should compile
    (void)result;

    // Test setState
    std::vector<uint8_t> new_state(1024, 0xAA);
    result = plugin->setState(new_state);
    (void)result;
}

TEST_F(VST3StateTest, VST3MemoryStreamRoundTrip) {
#if defined(HAVE_VST3_SDK)
    std::vector<uint8_t> buffer(4096);
    VST3MemoryStream stream(buffer);

    // Write various data types
    struct TestData {
        int32_t version = 1;
        float gain = 0.5f;
        float pan = -0.25f;
        bool enabled = true;
        char name[32] = "Test Plugin State";
    } data;

    int32_t written = 0;
    Steinberg::tresult result = stream.write(&data, sizeof(TestData), &written);
    EXPECT_EQ(result, Steinberg::kResultTrue);
    EXPECT_EQ(written, sizeof(TestData));

    // Verify by reading back
    stream.seek(0, 0, nullptr);  // Seek to start

    TestData read_data = {};
    int32_t read = 0;
    result = stream.read(&read_data, sizeof(TestData), &read);
    EXPECT_EQ(result, Steinberg::kResultTrue);
    EXPECT_EQ(read, sizeof(TestData));

    EXPECT_EQ(read_data.version, data.version);
    EXPECT_FLOAT_EQ(read_data.gain, data.gain);
    EXPECT_FLOAT_EQ(read_data.pan, data.pan);
    EXPECT_EQ(read_data.enabled, data.enabled);
    EXPECT_STREQ(read_data.name, data.name);
#endif
}

TEST_F(VST3StateTest, VST3MemoryStreamPartialReadWrite) {
#if defined(HAVE_VST3_SDK)
    std::vector<uint8_t> buffer(1024);
    VST3MemoryStream stream(buffer);

    // Write in chunks
    const char* chunk1 = "Header";
    const char* chunk2 = "Body";
    const char* chunk3 = "Footer";

    int32_t written = 0;
    stream.write(chunk1, 6, &written);
    EXPECT_EQ(written, 6);

    stream.write(chunk2, 4, &written);
    EXPECT_EQ(written, 4);

    stream.write(chunk3, 6, &written);
    EXPECT_EQ(written, 6);

    // Read back in different sized chunks
    stream.seek(0, 0, nullptr);

    char read_buf[20] = {0};
    int32_t read = 0;
    stream.read(read_buf, 3, &read);
    EXPECT_EQ(read, 3);
    EXPECT_STREQ(read_buf, "Hea");

    stream.read(read_buf, 10, &read);
    EXPECT_EQ(read, 10);
    EXPECT_STREQ(read_buf, "derBodyFoo");  // "der" + "BodyFoo"

    stream.read(read_buf, 6, &read);
    EXPECT_EQ(read, 6);
    EXPECT_STREQ(read_buf, "Footer");
#endif
}

TEST_F(VST3StateTest, VST3ControllerWrapperStateInterface) {
#if defined(HAVE_VST3_SDK)
    // Test that the controller wrapper state interface compiles correctly
    Steinberg::Vst::IEditController* controller = nullptr;
    VST3ControllerWrapper wrapper(controller);

    std::vector<uint8_t> state(1024, 0x55);

    // These should compile even if they return false
    bool result = wrapper.getState(state);
    (void)result;

    result = wrapper.setState(state);
    (void)result;

    Steinberg::Vst::IComponent* component = nullptr;
    result = wrapper.getComponentState(state, component);
    (void)result;

    result = wrapper.setComponentState(state, component);
    (void)result;
#endif
}

TEST_F(VST3StateTest, StateRoundTripWithoutSDK) {
    // Test that the interface compiles and runs (returns false without SDK)
    std::vector<uint8_t> original_state(256, 0x42);
    std::vector<uint8_t> retrieved_state;

    // getState should return false without SDK
    bool got_state = plugin->getState(retrieved_state);
    EXPECT_FALSE(got_state);

    // setState should return false without SDK
    bool set_result = plugin->setState(original_state);
    EXPECT_FALSE(set_result);
}

TEST_F(VST3StateTest, EmptyStateVector) {
    std::vector<uint8_t> empty_state;
    bool result = plugin->getState(empty_state);
    (void)result;

    result = plugin->setState(empty_state);
    (void)result;
}

TEST_F(VST3StateTest, LargeStateVector) {
    // Test with a larger state vector (simulating complex plugin state)
    std::vector<uint8_t> large_state(64 * 1024, 0xAA);  // 64KB

    bool result = plugin->getState(large_state);
    (void)result;

    result = plugin->setState(large_state);
    (void)result;
}

TEST_F(VST3StateTest, VST3MemoryStreamSeekModes) {
#if defined(HAVE_VST3_SDK)
    std::vector<uint8_t> buffer(100);
    VST3MemoryStream stream(buffer);

    // Fill with known data
    for (int i = 0; i < 100; ++i) {
        buffer[i] = static_cast<uint8_t>(i);
    }

    // Test kIBSeekSet (0)
    stream.seek(10, 0, nullptr);
    int64_t pos = 0;
    stream.tell(&pos);
    EXPECT_EQ(pos, 10);

    // Test kIBSeekCur (1)
    stream.seek(5, 1, nullptr);
    stream.tell(&pos);
    EXPECT_EQ(pos, 15);

    // Test kIBSeekEnd (2)
    stream.seek(-10, 2, nullptr);
    stream.tell(&pos);
    EXPECT_EQ(pos, 90);  // 100 - 10

    // Test boundary conditions
    stream.seek(-200, 0, nullptr);  // Before start
    stream.tell(&pos);
    EXPECT_EQ(pos, 0);

    stream.seek(200, 2, nullptr);   // After end
    stream.tell(&pos);
    EXPECT_EQ(pos, 100);
#endif
}

TEST_F(VST3StateTest, PluginStateMethodsExist) {
    // Compile-time test: verify all state methods exist on VST3Plugin
    std::vector<uint8_t> state;

    // These method calls should compile
    bool result = plugin->getState(state);
    (void)result;

    result = plugin->setState(state);
    (void)result;

    // Program state methods
    int32_t programs = plugin->getProgramCount();
    (void)programs;

    std::string name = plugin->getProgramName(0);
    (void)name;

    bool prog_result = plugin->setProgram(0);
    (void)prog_result;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}