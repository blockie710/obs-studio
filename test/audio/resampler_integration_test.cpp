/******************************************************************************
 * obs-community-studio: Resampler Integration Tests
 *
 * Validates frequency conversion fidelity and zero-drop buffering across
 * sample rate transitions.
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#include <gtest/gtest.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <thread>
#include <atomic>

// Include OBS audio resampler
extern "C" {
#include <media-io/audio-resampler.h>
#include <media-io/audio-io.h>
}

#define M_PI 3.14159265358979323846

class ResamplerIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Nothing special needed
    }

    void TearDown() override {
        // Nothing special needed
    }

    // Generate a sine wave at given frequency
    void GenerateSineWave(std::vector<float>& buffer, double frequency, double sample_rate, float amplitude = 1.0f) {
        for (size_t i = 0; i < buffer.size(); ++i) {
            double t = static_cast<double>(i) / sample_rate;
            buffer[i] = amplitude * std::sin(2.0 * M_PI * frequency * t);
        }
    }

    // Compute RMS of a buffer
    float ComputeRMS(const std::vector<float>& buffer) {
        double sum = 0.0;
        for (float v : buffer) {
            sum += static_cast<double>(v) * v;
        }
        return static_cast<float>(std::sqrt(sum / buffer.size()));
    }

    // Compute frequency using zero-crossing estimation
    double EstimateFrequency(const std::vector<float>& buffer, double sample_rate) {
        int zero_crossings = 0;
        for (size_t i = 1; i < buffer.size(); ++i) {
            if ((buffer[i-1] >= 0 && buffer[i] < 0) || (buffer[i-1] < 0 && buffer[i] >= 0)) {
                zero_crossings++;
            }
        }
        // Each cycle has 2 zero crossings
        return (zero_crossings / 2.0) * sample_rate / buffer.size();
    }
};

// Test basic resampler creation and destruction
TEST_F(ResamplerIntegrationTest, CreateDestroyResampler) {
    struct resample_info from = {
        .samples_per_sec = 48000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    struct resample_info to = {
        .samples_per_sec = 44100,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    audio_resampler_t* resampler = audio_resampler_create(&to, &from);
    ASSERT_NE(resampler, nullptr);

    audio_resampler_destroy(resampler);
}

// Test 48kHz -> 44.1kHz conversion (common case)
TEST_F(ResamplerIntegrationTest, Resample48kTo44_1k) {
    struct resample_info from = {
        .samples_per_sec = 48000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    struct resample_info to = {
        .samples_per_sec = 44100,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    audio_resampler_t* resampler = audio_resampler_create(&to, &from);
    ASSERT_NE(resampler, nullptr);

    const int input_frames = 1024;
    const int channels = 2;
    
    // Generate test signal: 1kHz sine wave
    std::vector<float> ch0_input(input_frames);
    std::vector<float> ch1_input(input_frames);
    GenerateSineWave(ch0_input, 1000.0, 48000.0, 0.5f);
    GenerateSineWave(ch1_input, 1000.0, 48000.0, 0.3f);

    const uint8_t* input_ptrs[2] = {
        reinterpret_cast<const uint8_t*>(ch0_input.data()),
        reinterpret_cast<const uint8_t*>(ch1_input.data())
    };

    // Output buffer - expect slightly fewer frames due to rate conversion
    int expected_out_frames = static_cast<int>(input_frames * 44100.0 / 48000.0) + 16;
    std::vector<float> ch0_output(expected_out_frames);
    std::vector<float> ch1_output(expected_out_frames);
    
    uint8_t* output_ptrs[2] = {
        reinterpret_cast<uint8_t*>(ch0_output.data()),
        reinterpret_cast<uint8_t*>(ch1_output.data())
    };

    uint32_t out_frames = 0;
    uint64_t ts_offset = 0;
    bool success = audio_resampler_resample(resampler, output_ptrs, &out_frames, &ts_offset,
                                            input_ptrs, input_frames);

    EXPECT_TRUE(success);
    EXPECT_GT(out_frames, 0);
    EXPECT_LE(out_frames, static_cast<uint32_t>(expected_out_frames));

    // Verify frequency is preserved (approximately 1kHz)
    double freq_ch0 = EstimateFrequency(std::vector<float>(ch0_output.begin(), ch0_output.begin() + out_frames), 44100.0);
    double freq_ch1 = EstimateFrequency(std::vector<float>(ch1_output.begin(), ch1_output.begin() + out_frames), 44100.0);
    
    EXPECT_NEAR(freq_ch0, 1000.0, 50.0);  // Within 50Hz tolerance
    EXPECT_NEAR(freq_ch1, 1000.0, 50.0);

    // Verify amplitude is preserved
    float rms_ch0 = ComputeRMS(std::vector<float>(ch0_output.begin(), ch0_output.begin() + out_frames));
    float rms_ch1 = ComputeRMS(std::vector<float>(ch1_output.begin(), ch1_output.begin() + out_frames));
    
    EXPECT_NEAR(rms_ch0, 0.5f / std::sqrt(2.0), 0.02f);
    EXPECT_NEAR(rms_ch1, 0.3f / std::sqrt(2.0), 0.02f);

    audio_resampler_destroy(resampler);
}

// Test 44.1kHz -> 48kHz conversion (upsampling)
TEST_F(ResamplerIntegrationTest, Resample44_1kTo48k) {
    struct resample_info from = {
        .samples_per_sec = 44100,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    struct resample_info to = {
        .samples_per_sec = 48000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    audio_resampler_t* resampler = audio_resampler_create(&to, &from);
    ASSERT_NE(resampler, nullptr);

    const int input_frames = 1024;
    
    // Generate test signal: 440Hz sine wave (A4)
    std::vector<float> ch0_input(input_frames);
    std::vector<float> ch1_input(input_frames);
    GenerateSineWave(ch0_input, 440.0, 44100.0, 0.7f);
    GenerateSineWave(ch1_input, 440.0, 44100.0, 0.5f);

    const uint8_t* input_ptrs[2] = {
        reinterpret_cast<const uint8_t*>(ch0_input.data()),
        reinterpret_cast<const uint8_t*>(ch1_input.data())
    };

    // Output buffer - expect slightly more frames due to upsampling
    int expected_out_frames = static_cast<int>(input_frames * 48000.0 / 44100.0) + 16;
    std::vector<float> ch0_output(expected_out_frames);
    std::vector<float> ch1_output(expected_out_frames);
    
    uint8_t* output_ptrs[2] = {
        reinterpret_cast<uint8_t*>(ch0_output.data()),
        reinterpret_cast<uint8_t*>(ch1_output.data())
    };

    uint32_t out_frames = 0;
    uint64_t ts_offset = 0;
    bool success = audio_resampler_resample(resampler, output_ptrs, &out_frames, &ts_offset,
                                            input_ptrs, input_frames);

    EXPECT_TRUE(success);
    EXPECT_GT(out_frames, 0);

    // Verify frequency is preserved
    double freq_ch0 = EstimateFrequency(std::vector<float>(ch0_output.begin(), ch0_output.begin() + out_frames), 48000.0);
    double freq_ch1 = EstimateFrequency(std::vector<float>(ch1_output.begin(), ch1_output.begin() + out_frames), 48000.0);
    
    EXPECT_NEAR(freq_ch0, 440.0, 30.0);
    EXPECT_NEAR(freq_ch1, 440.0, 30.0);

    audio_resampler_destroy(resampler);
}

// Test 96kHz -> 48kHz conversion (2x downsampling)
TEST_F(ResamplerIntegrationTest, Resample96kTo48k) {
    struct resample_info from = {
        .samples_per_sec = 96000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    struct resample_info to = {
        .samples_per_sec = 48000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    audio_resampler_t* resampler = audio_resampler_create(&to, &from);
    ASSERT_NE(resampler, nullptr);

    const int input_frames = 2048;
    
    // Generate test signal: 5kHz sine wave
    std::vector<float> ch0_input(input_frames);
    std::vector<float> ch1_input(input_frames);
    GenerateSineWave(ch0_input, 5000.0, 96000.0, 0.6f);
    GenerateSineWave(ch1_input, 5000.0, 96000.0, 0.4f);

    const uint8_t* input_ptrs[2] = {
        reinterpret_cast<const uint8_t*>(ch0_input.data()),
        reinterpret_cast<const uint8_t*>(ch1_input.data())
    };

    int expected_out_frames = input_frames / 2 + 16;
    std::vector<float> ch0_output(expected_out_frames);
    std::vector<float> ch1_output(expected_out_frames);
    
    uint8_t* output_ptrs[2] = {
        reinterpret_cast<uint8_t*>(ch0_output.data()),
        reinterpret_cast<uint8_t*>(ch1_output.data())
    };

    uint32_t out_frames = 0;
    uint64_t ts_offset = 0;
    bool success = audio_resampler_resample(resampler, output_ptrs, &out_frames, &ts_offset,
                                            input_ptrs, input_frames);

    EXPECT_TRUE(success);
    EXPECT_GT(out_frames, 0);
    EXPECT_NEAR(static_cast<double>(out_frames), input_frames / 2.0, 10.0);

    // Verify frequency is preserved
    double freq_ch0 = EstimateFrequency(std::vector<float>(ch0_output.begin(), ch0_output.begin() + out_frames), 48000.0);
    double freq_ch1 = EstimateFrequency(std::vector<float>(ch1_output.begin(), ch1_output.begin() + out_frames), 48000.0);
    
    EXPECT_NEAR(freq_ch0, 5000.0, 50.0);
    EXPECT_NEAR(freq_ch1, 5000.0, 50.0);

    audio_resampler_destroy(resampler);
}

// Test 192kHz -> 48kHz conversion (4x downsampling)
TEST_F(ResamplerIntegrationTest, Resample192kTo48k) {
    struct resample_info from = {
        .samples_per_sec = 192000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    struct resample_info to = {
        .samples_per_sec = 48000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    audio_resampler_t* resampler = audio_resampler_create(&to, &from);
    ASSERT_NE(resampler, nullptr);

    const int input_frames = 4096;
    
    // Generate test signal: 10kHz sine wave
    std::vector<float> ch0_input(input_frames);
    std::vector<float> ch1_input(input_frames);
    GenerateSineWave(ch0_input, 10000.0, 192000.0, 0.5f);
    GenerateSineWave(ch1_input, 10000.0, 192000.0, 0.5f);

    const uint8_t* input_ptrs[2] = {
        reinterpret_cast<const uint8_t*>(ch0_input.data()),
        reinterpret_cast<const uint8_t*>(ch1_input.data())
    };

    int expected_out_frames = input_frames / 4 + 16;
    std::vector<float> ch0_output(expected_out_frames);
    std::vector<float> ch1_output(expected_out_frames);
    
    uint8_t* output_ptrs[2] = {
        reinterpret_cast<uint8_t*>(ch0_output.data()),
        reinterpret_cast<uint8_t*>(ch1_output.data())
    };

    uint32_t out_frames = 0;
    uint64_t ts_offset = 0;
    bool success = audio_resampler_resample(resampler, output_ptrs, &out_frames, &ts_offset,
                                            input_ptrs, input_frames);

    EXPECT_TRUE(success);
    EXPECT_GT(out_frames, 0);
    EXPECT_NEAR(static_cast<double>(out_frames), input_frames / 4.0, 20.0);

    // Verify frequency is preserved
    double freq_ch0 = EstimateFrequency(std::vector<float>(ch0_output.begin(), ch0_output.begin() + out_frames), 48000.0);
    double freq_ch1 = EstimateFrequency(std::vector<float>(ch1_output.begin(), ch1_output.begin() + out_frames), 48000.0);
    
    EXPECT_NEAR(freq_ch0, 10000.0, 100.0);
    EXPECT_NEAR(freq_ch1, 10000.0, 100.0);

    audio_resampler_destroy(resampler);
}

// Test 88.2kHz -> 44.1kHz conversion (2x downsampling)
TEST_F(ResamplerIntegrationTest, Resample88_2kTo44_1k) {
    struct resample_info from = {
        .samples_per_sec = 88200,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    struct resample_info to = {
        .samples_per_sec = 44100,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    audio_resampler_t* resampler = audio_resampler_create(&to, &from);
    ASSERT_NE(resampler, nullptr);

    const int input_frames = 2048;
    
    // Generate test signal: 2kHz sine wave
    std::vector<float> ch0_input(input_frames);
    std::vector<float> ch1_input(input_frames);
    GenerateSineWave(ch0_input, 2000.0, 88200.0, 0.8f);
    GenerateSineWave(ch1_input, 2000.0, 88200.0, 0.6f);

    const uint8_t* input_ptrs[2] = {
        reinterpret_cast<const uint8_t*>(ch0_input.data()),
        reinterpret_cast<const uint8_t*>(ch1_input.data())
    };

    int expected_out_frames = input_frames / 2 + 16;
    std::vector<float> ch0_output(expected_out_frames);
    std::vector<float> ch1_output(expected_out_frames);
    
    uint8_t* output_ptrs[2] = {
        reinterpret_cast<uint8_t*>(ch0_output.data()),
        reinterpret_cast<uint8_t*>(ch1_output.data())
    };

    uint32_t out_frames = 0;
    uint64_t ts_offset = 0;
    bool success = audio_resampler_resample(resampler, output_ptrs, &out_frames, &ts_offset,
                                            input_ptrs, input_frames);

    EXPECT_TRUE(success);
    EXPECT_GT(out_frames, 0);

    // Verify frequency is preserved
    double freq_ch0 = EstimateFrequency(std::vector<float>(ch0_output.begin(), ch0_output.begin() + out_frames), 44100.0);
    double freq_ch1 = EstimateFrequency(std::vector<float>(ch1_output.begin(), ch1_output.begin() + out_frames), 44100.0);
    
    EXPECT_NEAR(freq_ch0, 2000.0, 30.0);
    EXPECT_NEAR(freq_ch1, 2000.0, 30.0);

    audio_resampler_destroy(resampler);
}

// Test multi-channel (5.1 surround) resampling
TEST_F(ResamplerIntegrationTest, Resample5_1Channels) {
    struct resample_info from = {
        .samples_per_sec = 96000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_5_1
    };

    struct resample_info to = {
        .samples_per_sec = 48000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_5_1
    };

    audio_resampler_t* resampler = audio_resampler_create(&to, &from);
    ASSERT_NE(resampler, nullptr);

    const int input_frames = 1024;
    const int channels = 6;  // 5.1 = 6 channels
    
    std::vector<std::vector<float>> inputs(channels);
    std::vector<std::vector<float>> outputs(channels);
    std::vector<const uint8_t*> input_ptrs(channels);
    std::vector<uint8_t*> output_ptrs(channels);

    for (int ch = 0; ch < channels; ++ch) {
        inputs[ch].resize(input_frames);
        outputs[ch].resize(input_frames / 2 + 16);
        GenerateSineWave(inputs[ch], 1000.0 + ch * 200.0, 96000.0, 0.5f);
        input_ptrs[ch] = reinterpret_cast<const uint8_t*>(inputs[ch].data());
        output_ptrs[ch] = reinterpret_cast<uint8_t*>(outputs[ch].data());
    }

    uint32_t out_frames = 0;
    uint64_t ts_offset = 0;
    bool success = audio_resampler_resample(resampler, output_ptrs.data(), &out_frames, &ts_offset,
                                            input_ptrs.data(), input_frames);

    EXPECT_TRUE(success);
    EXPECT_GT(out_frames, 0);

    // Verify all channels
    for (int ch = 0; ch < channels; ++ch) {
        double freq = EstimateFrequency(
            std::vector<float>(outputs[ch].begin(), outputs[ch].begin() + out_frames), 48000.0);
        EXPECT_NEAR(freq, 1000.0 + ch * 200.0, 50.0);
    }

    audio_resampler_destroy(resampler);
}

// Test mono resampling
TEST_F(ResamplerIntegrationTest, ResampleMono) {
    struct resample_info from = {
        .samples_per_sec = 48000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_MONO
    };

    struct resample_info to = {
        .samples_per_sec = 44100,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_MONO
    };

    audio_resampler_t* resampler = audio_resampler_create(&to, &from);
    ASSERT_NE(resampler, nullptr);

    const int input_frames = 1024;
    
    std::vector<float> ch0_input(input_frames);
    GenerateSineWave(ch0_input, 1000.0, 48000.0, 0.5f);

    const uint8_t* input_ptrs[1] = {
        reinterpret_cast<const uint8_t*>(ch0_input.data())
    };

    int expected_out_frames = static_cast<int>(input_frames * 44100.0 / 48000.0) + 16;
    std::vector<float> ch0_output(expected_out_frames);
    
    uint8_t* output_ptrs[1] = {
        reinterpret_cast<uint8_t*>(ch0_output.data())
    };

    uint32_t out_frames = 0;
    uint64_t ts_offset = 0;
    bool success = audio_resampler_resample(resampler, output_ptrs, &out_frames, &ts_offset,
                                            input_ptrs, input_frames);

    EXPECT_TRUE(success);
    EXPECT_GT(out_frames, 0);

    double freq = EstimateFrequency(
        std::vector<float>(ch0_output.begin(), ch0_output.begin() + out_frames), 44100.0);
    EXPECT_NEAR(freq, 1000.0, 50.0);

    audio_resampler_destroy(resampler);
}

// Test consecutive resampling operations (streaming)
TEST_F(ResamplerIntegrationTest, StreamingResample) {
    struct resample_info from = {
        .samples_per_sec = 96000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    struct resample_info to = {
        .samples_per_sec = 48000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    audio_resampler_t* resampler = audio_resampler_create(&to, &from);
    ASSERT_NE(resampler, nullptr);

    const int chunk_frames = 512;
    const int num_chunks = 20;
    const int channels = 2;
    
    uint32_t total_out_frames = 0;
    std::vector<float> all_output_ch0;
    std::vector<float> all_output_ch1;

    for (int chunk = 0; chunk < num_chunks; ++chunk) {
        std::vector<float> ch0_input(chunk_frames);
        std::vector<float> ch1_input(chunk_frames);
        // Continuing phase across chunks
        double phase_offset = chunk * chunk_frames / 96000.0;
        for (int i = 0; i < chunk_frames; ++i) {
            double t = (chunk * chunk_frames + i) / 96000.0;
            ch0_input[i] = 0.5f * std::sin(2.0 * M_PI * 1000.0 * t);
            ch1_input[i] = 0.3f * std::sin(2.0 * M_PI * 1000.0 * t);
        }

        const uint8_t* input_ptrs[2] = {
            reinterpret_cast<const uint8_t*>(ch0_input.data()),
            reinterpret_cast<const uint8_t*>(ch1_input.data())
        };

        std::vector<float> ch0_output(chunk_frames / 2 + 8);
        std::vector<float> ch1_output(chunk_frames / 2 + 8);
        uint8_t* output_ptrs[2] = {
            reinterpret_cast<uint8_t*>(ch0_output.data()),
            reinterpret_cast<uint8_t*>(ch1_output.data())
        };

        uint32_t out_frames = 0;
        uint64_t ts_offset = 0;
        bool success = audio_resampler_resample(resampler, output_ptrs, &out_frames, &ts_offset,
                                                input_ptrs, chunk_frames);

        EXPECT_TRUE(success);
        EXPECT_GT(out_frames, 0);

        all_output_ch0.insert(all_output_ch0.end(), ch0_output.begin(), ch0_output.begin() + out_frames);
        all_output_ch1.insert(all_output_ch1.end(), ch1_output.begin(), ch1_output.begin() + out_frames);
        total_out_frames += out_frames;
    }

    // Verify total output frames
    int expected_total = num_chunks * chunk_frames / 2;
    EXPECT_NEAR(static_cast<double>(total_out_frames), expected_total, 50.0);

    // Verify continuous frequency
    double freq_ch0 = EstimateFrequency(all_output_ch0, 48000.0);
    double freq_ch1 = EstimateFrequency(all_output_ch1, 48000.0);
    EXPECT_NEAR(freq_ch0, 1000.0, 30.0);
    EXPECT_NEAR(freq_ch1, 1000.0, 30.0);

    audio_resampler_destroy(resampler);
}

// Test zero input produces zero output
TEST_F(ResamplerIntegrationTest, ZeroInputProducesZeroOutput) {
    struct resample_info from = {
        .samples_per_sec = 48000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    struct resample_info to = {
        .samples_per_sec = 44100,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    audio_resampler_t* resampler = audio_resampler_create(&to, &from);
    ASSERT_NE(resampler, nullptr);

    const int input_frames = 512;
    std::vector<float> ch0_input(input_frames, 0.0f);
    std::vector<float> ch1_input(input_frames, 0.0f);

    const uint8_t* input_ptrs[2] = {
        reinterpret_cast<const uint8_t*>(ch0_input.data()),
        reinterpret_cast<const uint8_t*>(ch1_input.data())
    };

    std::vector<float> ch0_output(input_frames);
    std::vector<float> ch1_output(input_frames);
    uint8_t* output_ptrs[2] = {
        reinterpret_cast<uint8_t*>(ch0_output.data()),
        reinterpret_cast<uint8_t*>(ch1_output.data())
    };

    uint32_t out_frames = 0;
    uint64_t ts_offset = 0;
    bool success = audio_resampler_resample(resampler, output_ptrs, &out_frames, &ts_offset,
                                            input_ptrs, input_frames);

    EXPECT_TRUE(success);
    
    for (uint32_t i = 0; i < out_frames; ++i) {
        EXPECT_FLOAT_EQ(ch0_output[i], 0.0f);
        EXPECT_FLOAT_EQ(ch1_output[i], 0.0f);
    }

    audio_resampler_destroy(resampler);
}

// Test sample rate change detection (simulate hardware rate change)
TEST_F(ResamplerIntegrationTest, SampleRateChangeSimulation) {
    // First resampler at 48k -> 44.1k
    struct resample_info from1 = {
        .samples_per_sec = 48000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };
    struct resample_info to = {
        .samples_per_sec = 44100,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    audio_resampler_t* resampler1 = audio_resampler_create(&to, &from1);
    ASSERT_NE(resampler1, nullptr);

    // Process some data
    const int input_frames = 1024;
    std::vector<float> ch0_input(input_frames);
    std::vector<float> ch1_input(input_frames);
    GenerateSineWave(ch0_input, 1000.0, 48000.0, 0.5f);
    GenerateSineWave(ch1_input, 1000.0, 48000.0, 0.3f);

    const uint8_t* input_ptrs[2] = {
        reinterpret_cast<const uint8_t*>(ch0_input.data()),
        reinterpret_cast<const uint8_t*>(ch1_input.data())
    };

    std::vector<float> ch0_output(input_frames);
    std::vector<float> ch1_output(input_frames);
    uint8_t* output_ptrs[2] = {
        reinterpret_cast<uint8_t*>(ch0_output.data()),
        reinterpret_cast<uint8_t*>(ch1_output.data())
    };

    uint32_t out_frames = 0;
    uint64_t ts_offset = 0;
    audio_resampler_resample(resampler1, output_ptrs, &out_frames, &ts_offset,
                             input_ptrs, input_frames);

    audio_resampler_destroy(resampler1);

    // Now simulate hardware rate change to 96kHz
    // Create new resampler for 96k -> 44.1k
    struct resample_info from2 = {
        .samples_per_sec = 96000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    audio_resampler_t* resampler2 = audio_resampler_create(&to, &from2);
    ASSERT_NE(resampler2, nullptr);

    // Generate at 96kHz
    std::vector<float> ch0_input_96(input_frames * 2);
    std::vector<float> ch1_input_96(input_frames * 2);
    GenerateSineWave(ch0_input_96, 1000.0, 96000.0, 0.5f);
    GenerateSineWave(ch1_input_96, 1000.0, 96000.0, 0.3f);

    const uint8_t* input_ptrs_96[2] = {
        reinterpret_cast<const uint8_t*>(ch0_input_96.data()),
        reinterpret_cast<const uint8_t*>(ch1_input_96.data())
    };

    std::vector<float> ch0_output_96(input_frames * 2);
    std::vector<float> ch1_output_96(input_frames * 2);
    uint8_t* output_ptrs_96[2] = {
        reinterpret_cast<uint8_t*>(ch0_output_96.data()),
        reinterpret_cast<uint8_t*>(ch1_output_96.data())
    };

    out_frames = 0;
    ts_offset = 0;
    bool success = audio_resampler_resample(resampler2, output_ptrs_96, &out_frames, &ts_offset,
                                            input_ptrs_96, input_frames * 2);

    EXPECT_TRUE(success);
    EXPECT_GT(out_frames, 0);

    // Frequency should still be 1kHz
    double freq = EstimateFrequency(
        std::vector<float>(ch0_output_96.begin(), ch0_output_96.begin() + out_frames), 44100.0);
    EXPECT_NEAR(freq, 1000.0, 50.0);

    audio_resampler_destroy(resampler2);
}

// Test buffer size adaptation - small chunks
TEST_F(ResamplerIntegrationTest, SmallBufferChunks) {
    struct resample_info from = {
        .samples_per_sec = 48000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    struct resample_info to = {
        .samples_per_sec = 44100,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    audio_resampler_t* resampler = audio_resampler_create(&to, &from);
    ASSERT_NE(resampler, nullptr);

    const int small_chunk = 64;  // Very small buffer
    const int num_chunks = 100;
    
    uint32_t total_out = 0;

    for (int i = 0; i < num_chunks; ++i) {
        std::vector<float> ch0_input(small_chunk);
        std::vector<float> ch1_input(small_chunk);
        GenerateSineWave(ch0_input, 1000.0, 48000.0, 0.5f);
        GenerateSineWave(ch1_input, 1000.0, 48000.0, 0.3f);

        const uint8_t* input_ptrs[2] = {
            reinterpret_cast<const uint8_t*>(ch0_input.data()),
            reinterpret_cast<const uint8_t*>(ch1_input.data())
        };

        std::vector<float> ch0_output(small_chunk);
        std::vector<float> ch1_output(small_chunk);
        uint8_t* output_ptrs[2] = {
            reinterpret_cast<uint8_t*>(ch0_output.data()),
            reinterpret_cast<uint8_t*>(ch1_output.data())
        };

        uint32_t out_frames = 0;
        uint64_t ts_offset = 0;
        bool success = audio_resampler_resample(resampler, output_ptrs, &out_frames, &ts_offset,
                                                input_ptrs, small_chunk);

        EXPECT_TRUE(success);
        total_out += out_frames;
    }

    // Total output should be approximately num_chunks * small_chunk * 44100/48000
    double expected = num_chunks * small_chunk * 44100.0 / 48000.0;
    EXPECT_NEAR(static_cast<double>(total_out), expected, 50.0);

    audio_resampler_destroy(resampler);
}

// Test that resampler handles channel count mismatch gracefully
TEST_F(ResamplerIntegrationTest, ChannelMismatchHandling) {
    // This tests that the resampler handles the channel configuration correctly
    struct resample_info from = {
        .samples_per_sec = 48000,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    struct resample_info to = {
        .samples_per_sec = 44100,
        .format = AUDIO_FORMAT_FLOAT_PLANAR,
        .speakers = SPEAKERS_STEREO
    };

    audio_resampler_t* resampler = audio_resampler_create(&to, &from);
    ASSERT_NE(resampler, nullptr);

    // Try with only 1 channel input but stereo config
    // This should still work (second channel will be silence)
    const int input_frames = 512;
    std::vector<float> ch0_input(input_frames);
    std::vector<float> ch1_input(input_frames, 0.0f);  // Silent second channel
    GenerateSineWave(ch0_input, 1000.0, 48000.0, 0.5f);

    const uint8_t* input_ptrs[2] = {
        reinterpret_cast<const uint8_t*>(ch0_input.data()),
        reinterpret_cast<const uint8_t*>(ch1_input.data())
    };

    std::vector<float> ch0_output(input_frames);
    std::vector<float> ch1_output(input_frames);
    uint8_t* output_ptrs[2] = {
        reinterpret_cast<uint8_t*>(ch0_output.data()),
        reinterpret_cast<uint8_t*>(ch1_output.data())
    };

    uint32_t out_frames = 0;
    uint64_t ts_offset = 0;
    bool success = audio_resampler_resample(resampler, output_ptrs, &out_frames, &ts_offset,
                                            input_ptrs, input_frames);

    EXPECT_TRUE(success);
    EXPECT_GT(out_frames, 0);

    // First channel should have signal, second should be near zero
    float rms_ch0 = ComputeRMS(std::vector<float>(ch0_output.begin(), ch0_output.begin() + out_frames));
    float rms_ch1 = ComputeRMS(std::vector<float>(ch1_output.begin(), ch1_output.begin() + out_frames));
    
    EXPECT_GT(rms_ch0, 0.1f);
    EXPECT_LT(rms_ch1, 0.01f);

    audio_resampler_destroy(resampler);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}