/******************************************************************************
    Copyright (C) 2025 by OBS Studio Contributors.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace win_asio {

/**
 * Lock-free single-producer single-consumer ring buffer.
 * Capacity must be a power of 2.
 */
template<typename T, size_t Capacity>
class RingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static constexpr size_t Mask = Capacity - 1;

    std::atomic<size_t> writePos_{0};
    std::atomic<size_t> readPos_{0};
    T buffer_[Capacity];

public:
    RingBuffer() = default;
    ~RingBuffer() = default;
    
    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;
    
    /**
     * Push an item to the buffer (producer side).
     * @return true if pushed, false if buffer full
     */
    bool push(const T& item) {
        size_t w = writePos_.load(std::memory_order_relaxed);
        size_t next = (w + 1) & Mask;
        
        // Check if buffer would be full
        if (next == readPos_.load(std::memory_order_acquire)) {
            return false; // Full
        }
        
        buffer_[w] = item;
        writePos_.store(next, std::memory_order_release);
        return true;
    }
    
    /**
     * Try to push multiple items.
     * @return number of items actually pushed
     */
    size_t push(const T* items, size_t count) {
        size_t pushed = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!push(items[i])) {
                break;
            }
            pushed++;
        }
        return pushed;
    }
    
    /**
     * Pop an item from the buffer (consumer side).
     * @return true if popped, false if buffer empty
     */
    bool pop(T& item) {
        size_t r = readPos_.load(std::memory_order_relaxed);
        if (r == writePos_.load(std::memory_order_acquire)) {
            return false; // Empty
        }
        
        item = buffer_[r];
        readPos_.store((r + 1) & Mask, std::memory_order_release);
        return true;
    }
    
    /**
     * Try to pop multiple items.
     * @return number of items actually popped
     */
    size_t pop(T* items, size_t count) {
        size_t popped = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!pop(items[i])) {
                break;
            }
            popped++;
        }
        return popped;
    }
    
    /**
     * Get number of available items.
     */
    size_t available() const {
        size_t w = writePos_.load(std::memory_order_acquire);
        size_t r = readPos_.load(std::memory_order_acquire);
        return (w - r) & Mask;
    }
    
    /**
     * Get free space in buffer.
     */
    size_t freeSpace() const {
        return Capacity - 1 - available();
    }
    
    /**
     * Check if buffer is empty.
     */
    bool empty() const {
        return writePos_.load(std::memory_order_acquire) == readPos_.load(std::memory_order_acquire);
    }
    
    /**
     * Check if buffer is full.
     */
    bool full() const {
        size_t w = writePos_.load(std::memory_order_acquire);
        size_t next = (w + 1) & Mask;
        return next == readPos_.load(std::memory_order_acquire);
    }
    
    /**
     * Clear the buffer (producer side only).
     */
    void clear() {
        writePos_.store(0, std::memory_order_relaxed);
        readPos_.store(0, std::memory_order_relaxed);
    }
    
    /**
     * Get capacity of buffer.
     */
    static constexpr size_t capacity() { return Capacity; }
};

} // namespace win_asio