/******************************************************************************
 * obs-community-studio: ASIO Ring Buffer Unit Tests
 *
 * Tests for lock-free SPSC ring buffer implementation
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#include "asio-ring-buffer.hpp"

#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>
#include <vector>
#include <random>

using namespace win_asio;

class ASIORingBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        buffer = std::make_unique<ASIORingBuffer>(1024, 2);  // 1024 frames, 2 channels
    }

    void TearDown() override {
        buffer.reset();
    }

    std::unique_ptr<ASIORingBuffer> buffer;
};

TEST_F(ASIORingBufferTest, BasicPushPop) {
    std::vector<float> ch0(10, 1.0f);
    std::vector<float> ch1(10, 2.0f);
    float* planar[2] = { ch0.data(), ch1.data() };

    EXPECT_TRUE(buffer->Push(planar, 10));
    EXPECT_EQ(buffer->AvailableRead(), 10);
    EXPECT_EQ(buffer->AvailableWrite(), 1014);

    std::vector<float> out0(10), out1(10);
    float* out_planar[2] = { out0.data(), out1.data() };

    EXPECT_TRUE(buffer->Pop(out_planar, 10));
    EXPECT_EQ(buffer->AvailableRead(), 0);
    EXPECT_EQ(buffer->AvailableWrite(), 1024);

    for (int i = 0; i < 10; ++i) {
        EXPECT_FLOAT_EQ(out0[i], 1.0f);
        EXPECT_FLOAT_EQ(out1[i], 2.0f);
    }
}

TEST_F(ASIORingBufferTest, Wraparound) {
    // Fill nearly full
    const int chunk = 500;
    std::vector<float> data(chunk, 42.0f);
    float* planar[1] = { data.data() };

    for (int i = 0; i < 2; ++i) {
        EXPECT_TRUE(buffer->Push(planar, chunk));
    }
    EXPECT_EQ(buffer->AvailableRead(), chunk * 2);

    // Read all
    std::vector<float> out(chunk * 2);
    float* out_planar[1] = { out.data() };
    EXPECT_TRUE(buffer->Pop(out_planar, chunk * 2));
    EXPECT_EQ(buffer->AvailableRead(), 0);

    for (float v : out) {
        EXPECT_FLOAT_EQ(v, 42.0f);
    }
}

TEST_F(ASIORingBufferTest, OverrunDetection) {
    // Fill completely
    std::vector<float> data(1024, 1.0f);
    float* planar[1] = { data.data() };

    EXPECT_TRUE(buffer->Push(planar, 1024));
    EXPECT_FALSE(buffer->Push(planar, 1));  // Should fail - overrun

    auto stats = buffer->GetStats();
    EXPECT_EQ(stats.overruns, 1);
}

TEST_F(ASIORingBufferTest, UnderrunDetection) {
    // Try to read from empty buffer
    std::vector<float> out(10);
    float* out_planar[1] = { out.data() };

    EXPECT_FALSE(buffer->Pop(out_planar, 10));  // Should fail - underrun

    auto stats = buffer->GetStats();
    EXPECT_EQ(stats.underruns, 1);
}

TEST_F(ASIORingBufferTest, MultiChannel) {
    const int channels = 8;
    auto multi_buffer = std::make_unique<ASIORingBuffer>(512, channels);

    std::vector<std::vector<float>> input(channels);
    std::vector<float*> planar(channels);
    for (int ch = 0; ch < channels; ++ch) {
        input[ch].resize(100, float(ch + 1));
        planar[ch] = input[ch].data();
    }

    EXPECT_TRUE(multi_buffer->Push(planar.data(), 100));

    std::vector<std::vector<float>> output(channels);
    std::vector<float*> out_planar(channels);
    for (int ch = 0; ch < channels; ++ch) {
        output[ch].resize(100);
        out_planar[ch] = output[ch].data();
    }

    EXPECT_TRUE(multi_buffer->Pop(out_planar.data(), 100));

    for (int ch = 0; ch < channels; ++ch) {
        for (int i = 0; i < 100; ++i) {
            EXPECT_FLOAT_EQ(output[ch][i], float(ch + 1));
        }
    }
}

TEST_F(ASIORingBufferTest, ConcurrentProducerConsumer) {
    const int total_frames = 100000;
    const int chunk_size = 256;
    std::atomic<int> produced{0}, consumed{0};
    std::atomic<bool> stop{false};

    std::vector<float> ch0(chunk_size, 3.14f);
    std::vector<float> ch1(chunk_size, 2.71f);
    float* planar[2] = { ch0.data(), ch1.data() };

    std::thread producer([&]() {
        while (!stop && produced < total_frames) {
            int to_produce = std::min(chunk_size, total_frames - produced);
            if (buffer->Push(planar, to_produce)) {
                produced += to_produce;
            } else {
                std::this_thread::yield();
            }
        }
    });

    std::thread consumer([&]() {
        std::vector<float> out0(chunk_size), out1(chunk_size);
        float* out_planar[2] = { out0.data(), out1.data() };

        while (!stop || consumed < total_frames) {
            int available = buffer->AvailableRead();
            if (available > 0) {
                int to_consume = std::min(chunk_size, available);
                if (buffer->Pop(out_planar, to_consume)) {
                    consumed += to_consume;
                }
            } else {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(produced.load(), total_frames);
    EXPECT_EQ(consumed.load(), total_frames);
    EXPECT_EQ(buffer->AvailableRead(), 0);
}

TEST_F(ASIORingBufferTest, StatsTracking) {
    std::vector<float> data(100, 1.0f);
    float* planar[1] = { data.data() };

    buffer->Push(planar, 100);
    buffer->Pop(planar, 50);
    buffer->Push(planar, 200);  // This will overrun (only 50 free)

    auto stats = buffer->GetStats();
    EXPECT_EQ(stats.frames_written, 300);  // 100 + 200
    EXPECT_EQ(stats.frames_read, 50);
    EXPECT_EQ(stats.overruns, 1);
    EXPECT_EQ(stats.available_read, 150);  // 100 - 50 + 200 (but capped at capacity)
}

TEST_F(ASIORingBufferTest, Reset) {
    std::vector<float> data(50, 1.0f);
    float* planar[1] = { data.data() };

    buffer->Push(planar, 50);
    buffer->Reset();

    EXPECT_EQ(buffer->AvailableRead(), 0);
    EXPECT_EQ(buffer->AvailableWrite(), 1024);

    auto stats = buffer->GetStats();
    EXPECT_EQ(stats.frames_written, 0);
    EXPECT_EQ(stats.frames_read, 0);
    EXPECT_EQ(stats.overruns, 0);
    EXPECT_EQ(stats.underruns, 0);
}

TEST_F(ASIORingBufferTest, CapacityPowerOfTwo) {
    // Test various capacities
    for (int exp = 4; exp <= 14; ++exp) {
        int capacity = 1 << exp;
        auto buf = std::make_unique<ASIORingBuffer>(capacity, 2);
        EXPECT_EQ(buf->Capacity(), (size_t)capacity);

        std::vector<float> data(capacity, 1.0f);
        float* planar[2] = { data.data(), data.data() };

        EXPECT_TRUE(buf->Push(planar, capacity));
        EXPECT_EQ(buf->AvailableRead(), (size_t)capacity);
        EXPECT_TRUE(buf->Pop(planar, capacity));
        EXPECT_EQ(buf->AvailableRead(), 0);
    }
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}