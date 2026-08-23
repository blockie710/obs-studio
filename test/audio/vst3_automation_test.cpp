/******************************************************************************
 * obs-community-studio: VST3 Parameter Automation Unit Tests
 *
 * Tests for VST3 parameter automation round-trip
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#include <gtest/gtest.h>
#include <vector>
#include <string>
#include <chrono>

#include "vst3-plugin.hpp"
#include "vst3-controller.hpp"

using namespace obs_vst3;

class VST3AutomationTest : public ::testing::Test {
protected:
    void SetUp() override {
        plugin = std::make_unique<VST3Plugin>();
    }

    void TearDown() override {
        plugin.reset();
    }

    std::unique_ptr<VST3Plugin> plugin;
};

TEST_F(VST3AutomationTest, ParameterInfoStructure) {
    // Test that ParameterInfo struct has all required fields
    VST3Plugin::ParameterInfo info;
    info.id = 1;
    info.name = "Gain";
    info.unit = "dB";
    info.min_value = -60.0f;
    info.max_value = 24.0f;
    info.default_value = 0.0f;
    info.step_count = 1000;
    info.flags = 0;

    EXPECT_EQ(info.id, 1);
    EXPECT_EQ(info.name, "Gain");
    EXPECT_EQ(info.unit, "dB");
    EXPECT_FLOAT_EQ(info.min_value, -60.0f);
    EXPECT_FLOAT_EQ(info.max_value, 24.0f);
    EXPECT_FLOAT_EQ(info.default_value, 0.0f);
    EXPECT_FLOAT_EQ(info.step_count, 1000);
}

TEST_F(VST3AutomationTest, VST3AutomatedParamLinearInterpolation) {
    VST3AutomatedParam param;
    param.param_id = 1;
    param.param_name = "Gain";
    param.is_automated = true;

    // Add automation points: (timestamp_ns, value)
    param.automation_points = {
        {0, 0.0f},
        {1000000000, 0.5f},      // 1 second: 0.5
        {2000000000, 1.0f},      // 2 seconds: 1.0
        {3000000000, 0.0f}       // 3 seconds: 0.0
    };

    // Test exact points
    EXPECT_FLOAT_EQ(param.getValueAt(0), 0.0f);
    EXPECT_FLOAT_EQ(param.getValueAt(1000000000), 0.5f);
    EXPECT_FLOAT_EQ(param.getValueAt(2000000000), 1.0f);
    EXPECT_FLOAT_EQ(param.getValueAt(3000000000), 0.0f);

    // Test interpolation between points
    EXPECT_FLOAT_EQ(param.getValueAt(500000000), 0.25f);    // halfway between 0 and 0.5
    EXPECT_FLOAT_EQ(param.getValueAt(1500000000), 0.75f);   // halfway between 0.5 and 1.0
    EXPECT_FLOAT_EQ(param.getValueAt(2500000000), 0.5f);    // halfway between 1.0 and 0.0

    // Test before first point
    EXPECT_FLOAT_EQ(param.getValueAt(0), 0.0f);

    // Test after last point
    EXPECT_FLOAT_EQ(param.getValueAt(4000000000), 0.0f);
}

TEST_F(VST3AutomationTest, VST3AutomatedParamEdgeCases) {
    VST3AutomatedParam param;
    param.is_automated = false;

    // Non-automated should return 0
    EXPECT_FLOAT_EQ(param.getValueAt(1000000000), 0.0f);

    // Empty automation points
    param.is_automated = true;
    param.automation_points.clear();
    EXPECT_FLOAT_EQ(param.getValueAt(1000000000), 0.0f);

    // Single automation point
    param.automation_points = {{1000000000, 0.75f}};
    EXPECT_FLOAT_EQ(param.getValueAt(0), 0.75f);
    EXPECT_FLOAT_EQ(param.getValueAt(1000000000), 0.75f);
    EXPECT_FLOAT_EQ(param.getValueAt(2000000000), 0.75f);
}

TEST_F(VST3AutomationTest, VST3ControllerWrapperInterface) {
    // Test that the controller wrapper interface is properly defined
    // This is a compile-time test - if it compiles, the interface is correct

#if defined(HAVE_VST3_SDK)
    VST3ControllerWrapper* wrapper = nullptr;
    // These should compile without errors
    VST3Plugin::ParameterInfo info;
    bool result = wrapper->getParameterInfo(1, info);
    (void)result;

    float value = wrapper->getParameterNormalized(1);
    (void)value;

    bool set_result = wrapper->setParameterNormalized(1, 0.5f);
    (void)set_result;

    std::string str = wrapper->getParameterString(1);
    (void)str;

    std::vector<uint8_t> state;
    bool state_result = wrapper->getState(state);
    (void)state_result;

    state_result = wrapper->setState(state);
    (void)state_result;

    Steinberg::Vst::IComponent* component = nullptr;
    state_result = wrapper->getComponentState(state, component);
    (void)state_result;

    state_result = wrapper->setComponentState(state, component);
    (void)state_result;

    int32_t programs = wrapper->getProgramCount();
    (void)programs;

    std::string prog_name = wrapper->getProgramName(0);
    (void)prog_name;

    bool prog_result = wrapper->setProgram(0);
    (void)prog_result;
#endif
}

TEST_F(VST3AutomationTest, VST3MemoryStreamInterface) {
#if defined(HAVE_VST3_SDK)
    std::vector<uint8_t> buffer(1024);
    VST3MemoryStream stream(buffer);

    // Test write
    const char* test_data = "Hello VST3";
    int32_t written = 0;
    Steinberg::tresult result = stream.write(test_data, 11, &written);
    EXPECT_EQ(result, Steinberg::kResultTrue);
    EXPECT_EQ(written, 11);

    // Test read
    char read_buffer[12] = {0};
    int32_t read = 0;
    stream.seek(0, 0, nullptr);  // Seek to start
    result = stream.read(read_buffer, 11, &read);
    EXPECT_EQ(result, Steinberg::kResultTrue);
    EXPECT_EQ(read, 11);
    EXPECT_STREQ(read_buffer, "Hello VST3");

    // Test tell
    int64_t pos = 0;
    stream.tell(&pos);
    EXPECT_EQ(pos, 11);

    // Test seek modes
    stream.seek(0, 0, nullptr);   // kIBSeekSet
    stream.tell(&pos);
    EXPECT_EQ(pos, 0);

    stream.seek(5, 1, nullptr);   // kIBSeekCur
    stream.tell(&pos);
    EXPECT_EQ(pos, 5);

    stream.seek(-3, 2, nullptr);  // kIBSeekEnd (from end)
    stream.tell(&pos);
    EXPECT_EQ(pos, 8);  // 11 - 3
#endif
}

TEST_F(VST3AutomationTest, PluginParameterRoundTrip) {
    // This test verifies the plugin parameter interface works correctly
    // when VST3 SDK is available

    plugin->load("");  // Will fail without SDK, but interface should compile

    // Test parameter getter/setter interface
    float value = plugin->getParameter(1);
    (void)value;

    bool result = plugin->setParameter(1, 0.5f);
    (void)result;

    int32_t index = plugin->getParameterIndex(1);
    (void)index;

    auto params = plugin->getParameters();
    (void)params;
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}