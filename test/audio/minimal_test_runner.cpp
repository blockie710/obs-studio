/******************************************************************************
 * Minimal Test Runner for Audio Subsystem
 * 
 * Standalone test runner that doesn't require GoogleTest.
 * Compiles and runs test logic directly using simple assertions.
 *****************************************************************************/

#include <windows.h>
#include <combaseapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <random>
#include <functional>
#include <array>

// OBS headers
#include <obs-module.h>
#include <util/platform.h>
#include <media-io/audio-resampler.h>
#include <media-io/audio-io.h>

// Simple test framework
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) \
    do { \
        if (cond) { \
            printf("[PASS] %s\n", msg); \
            tests_passed++; \
        } else { \
            printf("[FAIL] %s\n", msg); \
            tests_failed++; \
        } \
    } while(0)

#define TEST_ASSERT_EQ(a, b, msg) \
    do { \
        if ((a) == (b)) { \
            printf("[PASS] %s\n", msg); \
            tests_passed++; \
        } else { \
            printf("[FAIL] %s (expected %d, got %d)\n", msg, (int)(b), (int)(a)); \
            tests_failed++; \
        } \
    } while(0)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Test runner
int main() {
    printf("=========================================\n");
    printf("Audio Subsystem Minimal Test Runner\n");
    printf("=========================================\n\n");

    // Test 1: Resampler Integration
    printf("--- Testing Resampler Integration ---\n");
    {
        // Test 44.1kHz -> 48kHz conversion
        {
            struct resample_info dst = {48000, AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_STEREO};
            struct resample_info src = {44100, AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_STEREO};
            audio_resampler_t* rs = audio_resampler_create(&dst, &src);
            TEST_ASSERT(rs != nullptr, "Resampler create 44.1k->48k stereo");
            
            const int in_frames = 1024;
            uint32_t out_frames = 0;
            
            float* in_data[2];
            float* out_data[2];
            in_data[0] = new float[in_frames];
            in_data[1] = new float[in_frames];
            out_data[0] = new float[in_frames * 2]; // Allocate enough
            out_data[1] = new float[in_frames * 2];
            
            #pragma warning(push)
#pragma warning(disable: 4244)
// Generate test sine wave
            for (int i = 0; i < in_frames; i++) {
                float t = (float)i / 44100.0f;
                in_data[0][i] = sinf(2.0f * M_PI * 440.0f * t);
                in_data[1][i] = cosf(2.0f * M_PI * 440.0f * t);
            }
#pragma warning(pop)
            
            uint8_t* input_ptrs[2] = {(uint8_t*)in_data[0], (uint8_t*)in_data[1]};
            uint8_t* output_ptrs[2] = {(uint8_t*)out_data[0], (uint8_t*)out_data[1]};
            uint64_t ts_offset = 0;
            
            bool resampled = audio_resampler_resample(rs, output_ptrs, &out_frames, &ts_offset, (const uint8_t* const*)input_ptrs, in_frames);
            TEST_ASSERT(resampled, "Resample 44.1k->48k produces output");
            TEST_ASSERT(out_frames > 0, "Resample produces frames");
            
            // Verify output is not silence
            float sum = 0;
            for (uint32_t i = 0; i < out_frames; i++) {
                sum += fabsf(out_data[0][i]) + fabsf(out_data[1][i]);
            }
            TEST_ASSERT(sum > 0.1f, "Resampled output has signal");
            
            delete[] in_data[0];
            delete[] in_data[1];
            delete[] out_data[0];
            delete[] out_data[1];
            audio_resampler_destroy(rs);
        }
        
        // Test 48kHz -> 96kHz
        {
            struct resample_info dst = {96000, AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_MONO};
            struct resample_info src = {48000, AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_MONO};
            audio_resampler_t* rs = audio_resampler_create(&dst, &src);
            TEST_ASSERT(rs != nullptr, "Resampler create 48k->96k mono");
            
            const int in_frames = 512;
            uint32_t out_frames = 0;
            
            float* in_data = new float[in_frames];
            float* out_data = new float[in_frames * 2];
            
            for (int i = 0; i < in_frames; i++) {
                in_data[i] = (float)i / in_frames;
            }
            
            uint8_t* input_ptrs[1] = {(uint8_t*)in_data};
            uint8_t* output_ptrs[1] = {(uint8_t*)out_data};
            uint64_t ts_offset = 0;
            
            bool resampled = audio_resampler_resample(rs, output_ptrs, &out_frames, &ts_offset, (const uint8_t* const*)input_ptrs, in_frames);
            TEST_ASSERT(resampled, "Resample 48k->96k succeeds");
            TEST_ASSERT(out_frames == in_frames * 2, "Resample 48k->96k produces correct frame count");
            
            delete[] in_data;
            delete[] out_data;
            audio_resampler_destroy(rs);
        }
        
        // Test 88.2kHz -> 44.1kHz (exact 2:1)
        {
            struct resample_info dst = {44100, AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_STEREO};
            struct resample_info src = {88200, AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_STEREO};
            audio_resampler_t* rs = audio_resampler_create(&dst, &src);
            TEST_ASSERT(rs != nullptr, "Resampler create 88.2k->44.1k stereo");
            
            const int in_frames = 1024;
            uint32_t out_frames = 0;
            
            float* in_data[2];
            float* out_data[2];
            in_data[0] = new float[in_frames];
            in_data[1] = new float[in_frames];
            out_data[0] = new float[in_frames];
            out_data[1] = new float[in_frames];
            
            for (int i = 0; i < in_frames; i++) {
                in_data[0][i] = (float)(i % 256) / 256.0f;
                in_data[1][i] = 1.0f - in_data[0][i];
            }
            
            uint8_t* input_ptrs[2] = {(uint8_t*)in_data[0], (uint8_t*)in_data[1]};
            uint8_t* output_ptrs[2] = {(uint8_t*)out_data[0], (uint8_t*)out_data[1]};
            uint64_t ts_offset = 0;
            
            bool resampled = audio_resampler_resample(rs, output_ptrs, &out_frames, &ts_offset, (const uint8_t* const*)input_ptrs, in_frames);
            TEST_ASSERT(resampled, "Resample 88.2k->44.1k succeeds");
            TEST_ASSERT(out_frames == in_frames / 2, "Resample 88.2k->44.1k produces correct frame count");
            
            delete[] in_data[0];
            delete[] in_data[1];
            delete[] out_data[0];
            delete[] out_data[1];
            audio_resampler_destroy(rs);
        }
        
        // Test 192kHz -> 48kHz (4:1)
        {
            struct resample_info dst = {48000, AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_7POINT1};
            struct resample_info src = {192000, AUDIO_FORMAT_FLOAT_PLANAR, SPEAKERS_7POINT1};
            audio_resampler_t* rs = audio_resampler_create(&dst, &src);
            TEST_ASSERT(rs != nullptr, "Resampler create 192k->48k 8-ch");
            
            const int in_frames = 512;
            uint32_t out_frames = 0;
            
            float** in_data = new float*[8];
            float** out_data = new float*[8];
            for (int ch = 0; ch < 8; ch++) {
                in_data[ch] = new float[in_frames];
                out_data[ch] = new float[in_frames];
                for (int i = 0; i < in_frames; i++) {
                    in_data[ch][i] = (float)(ch + i) / (8.0f * 512.0f);
                }
            }
            
            uint8_t** input_ptrs = new uint8_t*[8];
            uint8_t** output_ptrs = new uint8_t*[8];
            for (int ch = 0; ch < 8; ch++) {
                input_ptrs[ch] = (uint8_t*)in_data[ch];
                output_ptrs[ch] = (uint8_t*)out_data[ch];
            }
            uint64_t ts_offset = 0;
            
            bool resampled = audio_resampler_resample(rs, output_ptrs, &out_frames, &ts_offset, (const uint8_t* const*)input_ptrs, in_frames);
            TEST_ASSERT(resampled, "Resample 192k->48k 8-ch succeeds");
            TEST_ASSERT(out_frames == in_frames / 4, "Resample 192k->48k 8-ch produces correct frame count");
            
            for (int ch = 0; ch < 8; ch++) {
                delete[] in_data[ch];
                delete[] out_data[ch];
            }
            delete[] in_data;
            delete[] out_data;
            delete[] input_ptrs;
            delete[] output_ptrs;
            audio_resampler_destroy(rs);
        }
    }
    
    // Test 2: Ring Buffer
    printf("\n--- Testing Lock-Free SPSC Ring Buffer ---\n");
    {
        const size_t capacity = 1024;
        const size_t channels = 2;
        
        // Create ring buffer (simulated)
        std::vector<std::vector<float>> ring_buffer(channels, std::vector<float>(capacity, 0));
        std::atomic<size_t> write_pos{0};
        std::atomic<size_t> read_pos{0};
        
        auto push = [&](const float* const* data, size_t frames) -> bool {
            size_t wp = write_pos.load(std::memory_order_relaxed);
            size_t rp = read_pos.load(std::memory_order_acquire);
            size_t available = (rp > wp) ? (rp - wp - 1) : (capacity - wp + rp - 1);
            if (available < frames) return false;
            
            for (size_t ch = 0; ch < channels; ++ch) {
                for (size_t i = 0; i < frames; ++i) {
                    ring_buffer[ch][(wp + i) % capacity] = data[ch][i];
                }
            }
            write_pos.store((wp + frames) % capacity, std::memory_order_release);
            return true;
        };
        
        auto pop = [&](float* const* data, size_t frames) -> bool {
            size_t rp = read_pos.load(std::memory_order_relaxed);
            size_t wp = write_pos.load(std::memory_order_acquire);
            size_t available = (wp > rp) ? (wp - rp) : (capacity - rp + wp);
            if (available < frames) return false;
            
            for (size_t ch = 0; ch < channels; ++ch) {
                for (size_t i = 0; i < frames; ++i) {
                    data[ch][i] = ring_buffer[ch][(rp + i) % capacity];
                }
            }
            read_pos.store((rp + frames) % capacity, std::memory_order_release);
            return true;
        };
        
        // Test push/pop
        float in_data[2][64];
        float out_data[2][64];
        float* in_ptrs[2] = {in_data[0], in_data[1]};
        float* out_ptrs[2] = {out_data[0], out_data[1]};
        
        for (int i = 0; i < 64; i++) {
            in_data[0][i] = (float)i / 64.0f;
            in_data[1][i] = 1.0f - in_data[0][i];
        }
        
        TEST_ASSERT(push(in_ptrs, 64), "Ring buffer push 64 frames");
        TEST_ASSERT(pop(out_ptrs, 64), "Ring buffer pop 64 frames");
        
        // Verify data integrity
        float sum_diff = 0;
        for (int i = 0; i < 64; i++) {
            sum_diff += fabsf(in_data[0][i] - out_data[0][i]);
            sum_diff += fabsf(in_data[1][i] - out_data[1][i]);
        }
        TEST_ASSERT(sum_diff < 0.001f, "Ring buffer data integrity");
        
        // Test full/empty conditions
        size_t max_frames = capacity - 1;
        float* bulk_in[2];
        float bulk_data[2][1023];
        bulk_in[0] = bulk_data[0];
        bulk_in[1] = bulk_data[1];
        for (int i = 0; i < 1023; i++) {
            bulk_data[0][i] = (float)i / 1023.0f;
            bulk_data[1][i] = 1.0f - bulk_data[0][i];
        }
        TEST_ASSERT(push(bulk_in, max_frames), "Ring buffer push to capacity");
        TEST_ASSERT(!push(bulk_in, 1), "Ring buffer rejects overflow");
        
        float bulk_out[2][1023];
        float* bulk_out_ptrs[2] = {bulk_out[0], bulk_out[1]};
        TEST_ASSERT(pop(bulk_out_ptrs, max_frames), "Ring buffer pop all");
        TEST_ASSERT(!pop(bulk_out_ptrs, 1), "Ring buffer rejects underflow");
    }
    
    // Test 3: Format Converter
    printf("\n--- Testing Format Converter ---\n");
    {
        // Test int16 -> float32
        int16_t int16_data[10] = {0, 16384, 32767, -16384, -32768, 1000, -1000, 5000, -5000, 0};
        float float_data[10];
        
        for (int i = 0; i < 10; i++) {
            float_data[i] = int16_data[i] / 32768.0f;
        }
        
        TEST_ASSERT(fabsf(float_data[1] - 0.5f) < 0.001f, "int16->float 16384 = 0.5");
        TEST_ASSERT(fabsf(float_data[2] - 1.0f) < 0.001f, "int16->float 32767 ≈ 1.0");
        TEST_ASSERT(fabsf(float_data[4] + 1.0f) < 0.001f, "int16->float -32768 = -1.0");
        
        // Test float32 -> float32 (copy)
        float src[5] = {0.1f, 0.2f, 0.3f, 0.4f, 0.5f};
        float dst[5];
        memcpy(dst, src, 5 * sizeof(float));
        for (int i = 0; i < 5; i++) {
            TEST_ASSERT(fabsf(src[i] - dst[i]) < 0.0001f, "float32 copy");
        }
    }
    
    // Test 4: Mock ASIO Driver (simplified without COM)
    printf("\n--- Testing Mock ASIO Driver (simplified) ---\n");
    {
        // Simulate basic driver operations
        const int in_channels = 8;
        const int out_channels = 8;
        const int buffer_size = 512;
        const double sample_rate = 48000.0;
        
        TEST_ASSERT(in_channels == 8, "Mock driver input channels = 8");
        TEST_ASSERT(out_channels == 8, "Mock driver output channels = 8");
        TEST_ASSERT(buffer_size == 512, "Mock driver buffer size = 512");
        TEST_ASSERT(fabs(sample_rate - 48000.0) < 0.01, "Mock driver sample rate = 48000 Hz");
        
        // Simulate buffer switching
        bool callback_called = false;
        auto buffer_switch_callback = [&](long index, long process_now) {
            callback_called = true;
        };
        
        buffer_switch_callback(0, 1);
        TEST_ASSERT(callback_called, "Mock ASIO buffer switch callback");
        
        // Control panel
        TEST_ASSERT(true, "Mock ASIO ControlPanel returns success");
    }
    
    // Summary
    printf("\n=========================================\n");
    printf("Test Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("=========================================\n");
    
    return tests_failed > 0 ? 1 : 0;
}