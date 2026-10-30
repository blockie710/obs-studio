/******************************************************************************
 * win-asio: ASIO Ring Buffer Header
 *
 * Lock-free SPSC ring buffer for real-time audio transport
 * Zero-allocation, cache-line aligned for NUMA-friendly access
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace win_asio {

class ASIORingBuffer {
public:
    struct Stats {
        uint64_t frames_written = 0;
        uint64_t frames_read = 0;
        uint64_t overruns = 0;
        uint64_t underruns = 0;
        size_t available_read = 0;
        size_t available_write = 0;
    };

    // Capacity must be power of 2 for efficient modulo via bitwise AND
    explicit ASIORingBuffer(size_t capacity_frames, size_t num_channels);
    ~ASIORingBuffer();

    // Non-copyable, non-movable
    ASIORingBuffer(const ASIORingBuffer&) = delete;
    ASIORingBuffer& operator=(const ASIORingBuffer&) = delete;
    ASIORingBuffer(ASIORingBuffer&&) = delete;
    ASIORingBuffer& operator=(ASIORingBuffer&&) = delete;

    // Push planar float32 frames (non-blocking, returns false if full)
    // planar_input[channel][frame]
    bool Push(const float* const* planar_input, size_t num_frames);

    // Pop planar float32 frames (non-blocking, returns false if empty)
    // planar_output[channel][frame]
    bool Pop(float* const* planar_output, size_t num_frames);

    // Query available frames (thread-safe)
    size_t AvailableRead() const noexcept;
    size_t AvailableWrite() const noexcept;

    // Reset buffer (call only when no concurrent access)
    void Reset() noexcept;

    // Statistics
    Stats GetStats() const noexcept;

    // Properties
    size_t Capacity() const noexcept { return capacity_; }
    size_t ChannelCount() const noexcept { return channel_count_; }
    size_t FrameStride() const noexcept { return frame_stride_; }

private:
    // Power-of-2 capacity for fast modulo
    const size_t capacity_;
    const size_t channel_count_;
    const size_t frame_stride_;

    // Aligned audio data buffer
    uint8_t* data_;

    // SPSC indices (cache-line aligned via member alignment)
    alignas(64) std::atomic<size_t> write_pos_;
    alignas(64) std::atomic<size_t> read_pos_;

    // Statistics (cache-line aligned)
    alignas(64) std::atomic<uint64_t> frames_written_;
    alignas(64) std::atomic<uint64_t> frames_read_;
    alignas(64) std::atomic<uint64_t> overruns_;
    alignas(64) std::atomic<uint64_t> underruns_;
};

} // namespace win_asio