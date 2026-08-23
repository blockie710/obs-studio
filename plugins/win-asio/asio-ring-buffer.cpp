/******************************************************************************
 * win-asio: ASIO Ring Buffer Implementation
 *
 * Lock-free SPSC ring buffer for real-time audio transport
 * Zero-allocation, cache-line aligned for NUMA-friendly access
 *
 * Copyright (C) 2024 Community Contributors
 * GNU GPLv3 (or later)
 *****************************************************************************/

#include "asio-ring-buffer.hpp"

#include <cstdlib>
#include <cstring>
#include <new>

namespace win_asio {

// Cache line size for alignment
constexpr size_t CACHE_LINE_SIZE = 64;

// Aligned allocation helper
static void* aligned_alloc_cache(size_t size, size_t alignment = CACHE_LINE_SIZE) {
    void* ptr = nullptr;
#if defined(_WIN32)
    ptr = _aligned_malloc(size, alignment);
#else
    if (posix_memalign(&ptr, alignment, size) != 0) ptr = nullptr;
#endif
    return ptr;
}

static void aligned_free(void* ptr) {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

ASIORingBuffer::ASIORingBuffer(size_t capacity_frames, size_t num_channels)
    : capacity_(capacity_frames)
    , channel_count_(num_channels)
    , frame_stride_(num_channels * sizeof(float))
    , data_(static_cast<uint8_t*>(aligned_alloc_cache(capacity_frames * frame_stride_)))
    , write_pos_(0)
    , read_pos_(0)
    , frames_written_(0)
    , frames_read_(0)
    , overruns_(0)
    , underruns_(0)
{
    if (!data_) {
        throw std::bad_alloc();
    }
    // Zero initialize
    std::memset(data_, 0, capacity_frames * frame_stride_);
}

ASIORingBuffer::~ASIORingBuffer() {
    aligned_free(data_);
    data_ = nullptr;
}

bool ASIORingBuffer::Push(const float* const* planar_input, size_t num_frames) {
    if (num_frames == 0) return true;

    size_t write_pos = write_pos_.load(std::memory_order_relaxed);
    size_t read_pos = read_pos_.load(std::memory_order_acquire);

    size_t available = capacity_ - ((write_pos - read_pos) & (capacity_ - 1));
    if (num_frames > available) {
        overruns_.fetch_add(1, std::memory_order_relaxed);
        return false;  // Buffer full
    }

    // Write frames
    for (size_t frame = 0; frame < num_frames; ++frame) {
        size_t dst_offset = ((write_pos + frame) & (capacity_ - 1)) * frame_stride_;
        for (size_t ch = 0; ch < channel_count_; ++ch) {
            float* dst = reinterpret_cast<float*>(data_ + dst_offset + ch * sizeof(float));
            *dst = planar_input[ch][frame];
        }
    }

    write_pos_.store((write_pos + num_frames) & (capacity_ - 1), std::memory_order_release);
    frames_written_.fetch_add(num_frames, std::memory_order_relaxed);
    return true;
}

bool ASIORingBuffer::Pop(float* const* planar_output, size_t num_frames) {
    if (num_frames == 0) return true;

    size_t read_pos = read_pos_.load(std::memory_order_relaxed);
    size_t write_pos = write_pos_.load(std::memory_order_acquire);

    size_t available = (write_pos - read_pos) & (capacity_ - 1);
    if (num_frames > available) {
        underruns_.fetch_add(1, std::memory_order_relaxed);
        return false;  // Buffer empty
    }

    // Read frames
    for (size_t frame = 0; frame < num_frames; ++frame) {
        size_t src_offset = ((read_pos + frame) & (capacity_ - 1)) * frame_stride_;
        for (size_t ch = 0; ch < channel_count_; ++ch) {
            const float* src = reinterpret_cast<const float*>(data_ + src_offset + ch * sizeof(float));
            planar_output[ch][frame] = *src;
        }
    }

    read_pos_.store((read_pos + num_frames) & (capacity_ - 1), std::memory_order_release);
    frames_read_.fetch_add(num_frames, std::memory_order_relaxed);
    return true;
}

size_t ASIORingBuffer::AvailableRead() const noexcept {
    size_t write_pos = write_pos_.load(std::memory_order_acquire);
    size_t read_pos = read_pos_.load(std::memory_order_relaxed);
    return (write_pos - read_pos) & (capacity_ - 1);
}

size_t ASIORingBuffer::AvailableWrite() const noexcept {
    size_t write_pos = write_pos_.load(std::memory_order_relaxed);
    size_t read_pos = read_pos_.load(std::memory_order_acquire);
    return capacity_ - ((write_pos - read_pos) & (capacity_ - 1));
}

void ASIORingBuffer::Reset() noexcept {
    write_pos_.store(0, std::memory_order_relaxed);
    read_pos_.store(0, std::memory_order_relaxed);
    frames_written_.store(0, std::memory_order_relaxed);
    frames_read_.store(0, std::memory_order_relaxed);
    overruns_.store(0, std::memory_order_relaxed);
    underruns_.store(0, std::memory_order_relaxed);
    std::memset(data_, 0, capacity_ * frame_stride_);
}

ASIORingBuffer::Stats ASIORingBuffer::GetStats() const noexcept {
    Stats stats;
    stats.frames_written = frames_written_.load(std::memory_order_relaxed);
    stats.frames_read = frames_read_.load(std::memory_order_relaxed);
    stats.overruns = overruns_.load(std::memory_order_relaxed);
    stats.underruns = underruns_.load(std::memory_order_relaxed);
    stats.available_read = AvailableRead();
    stats.available_write = AvailableWrite();
    return stats;
}

} // namespace win_asio