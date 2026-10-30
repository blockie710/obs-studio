/******************************************************************************
 * obs-community-studio: Format Converter Unit Tests
 *
 * Tests for ASIO sample format to planar float32 conversion
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#include <gtest/gtest.h>
#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace win_asio {

// Mirror the conversion functions from asio-driver-manager.cpp for testing
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

} // namespace win_asio

using namespace win_asio;

TEST(FormatConverter, Int16ToFloat32) {
    std::vector<int16_t> input = {0, 32767, -32768, 16384, -16384};
    std::vector<float> output(5);

    convert_int16_to_float32(input.data(), output.data(), 5);

    EXPECT_FLOAT_EQ(output[0], 0.0f);
    EXPECT_NEAR(output[1], 1.0f, 1e-6f);      // Max positive
    EXPECT_NEAR(output[2], -1.0f, 1e-6f);     // Max negative
    EXPECT_NEAR(output[3], 0.5f, 1e-6f);      // Half scale
    EXPECT_NEAR(output[4], -0.5f, 1e-6f);     // Half scale negative
}

TEST(FormatConverter, Int24ToFloat32) {
    // 24-bit values: max positive = 0x7FFFFF, max negative = 0x800000
    std::vector<uint8_t> input = {
        0x7F, 0xFF, 0xFF,   // Max positive
        0x80, 0x00, 0x00,   // Max negative
        0x40, 0x00, 0x00,   // Quarter scale
        0x00, 0x00, 0x00,   // Zero
        0xC0, 0x00, 0x00    // -0.5
    };
    std::vector<float> output(5);

    convert_int24_to_float32(input.data(), output.data(), 5);

    EXPECT_NEAR(output[0], 1.0f, 1e-6f);       // Max positive
    EXPECT_NEAR(output[1], -1.0f, 1e-6f);      // Max negative
    EXPECT_NEAR(output[2], 0.25f, 1e-6f);      // Quarter scale
    EXPECT_FLOAT_EQ(output[3], 0.0f);          // Zero
    EXPECT_NEAR(output[4], -0.5f, 1e-6f);      // -0.5
}

TEST(FormatConverter, Int32ToFloat32) {
    std::vector<int32_t> input = {0, 2147483647, -2147483648, 1073741824, -1073741824};
    std::vector<float> output(5);

    convert_int32_to_float32(input.data(), output.data(), 5);

    EXPECT_FLOAT_EQ(output[0], 0.0f);
    EXPECT_NEAR(output[1], 1.0f, 1e-6f);
    EXPECT_NEAR(output[2], -1.0f, 1e-6f);
    EXPECT_NEAR(output[3], 0.5f, 1e-6f);
    EXPECT_NEAR(output[4], -0.5f, 1e-6f);
}

TEST(FormatConverter, Float32ToFloat32) {
    std::vector<float> input = {0.0f, 1.0f, -1.0f, 0.5f, -0.5f, 0.123f};
    std::vector<float> output(6);

    convert_float32_to_float32(input.data(), output.data(), 6);

    for (int i = 0; i < 6; ++i) {
        EXPECT_FLOAT_EQ(output[i], input[i]);
    }
}

TEST(FormatConverter, Float64ToFloat32) {
    std::vector<double> input = {0.0, 1.0, -1.0, 0.5, -0.5, M_PI};
    std::vector<float> output(6);

    convert_float64_to_float32(input.data(), output.data(), 6);

    for (int i = 0; i < 6; ++i) {
        EXPECT_NEAR(output[i], static_cast<float>(input[i]), 1e-6f);
    }
}

TEST(FormatConverter, LargeBuffer) {
    const int size = 10000;
    std::vector<int16_t> input(size);
    std::vector<float> output(size);

    // Fill with sine wave
    for (int i = 0; i < size; ++i) {
        input[i] = static_cast<int16_t>(32767.0f * sinf(2.0f * M_PI * i / 100.0f));
    }

    convert_int16_to_float32(input.data(), output.data(), size);

    // Verify a few points
    EXPECT_NEAR(output[0], 0.0f, 1e-4f);
    EXPECT_NEAR(output[25], 1.0f, 1e-3f);  // Near peak
    EXPECT_NEAR(output[75], -1.0f, 1e-3f); // Near trough
}

TEST(FormatConverter, ZeroInput) {
    std::vector<int32_t> input(100, 0);
    std::vector<float> output(100);

    convert_int32_to_float32(input.data(), output.data(), 100);

    for (float v : output) {
        EXPECT_FLOAT_EQ(v, 0.0f);
    }
}

TEST(FormatConverter, AlternatingSigns) {
    std::vector<int16_t> input = {1000, -1000, 2000, -2000, 3000, -3000};
    std::vector<float> output(6);

    convert_int16_to_float32(input.data(), output.data(), 6);

    for (int i = 0; i < 6; ++i) {
        EXPECT_FLOAT_EQ(output[i], (i % 2 == 0) ? fabsf(output[i]) : -fabsf(output[i]));
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}