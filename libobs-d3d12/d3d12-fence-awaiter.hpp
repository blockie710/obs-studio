/******************************************************************************
    SPDX-FileCopyrightText: Copyright (c) 2026 LibreCast Studio Contributors
    SPDX-License-Identifier: GPL-3.0-or-later

    Phase 4: DirectX 12 GPU Timeline Fence Coroutine Awaiter
    Location: libobs-d3d12/d3d12-fence-awaiter.hpp
******************************************************************************/

#pragma once

#include "../shared/util/windows-hygiene.hpp"
#include "../libobs/util/windows/HRError.hpp"
#include "../libobs/graphics/texture-handle.h"

#include <coroutine>
#include <cstdint>
#ifndef near
#define near
#endif
#ifndef far
#define far
#endif
#ifndef interface
#define interface struct
#endif
#include <d3d12.h>
#ifdef small
#undef small
#endif
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
#ifdef interface
#undef interface
#endif

namespace librecast::d3d12 {

/**
 * Non-blocking C++23 coroutine awaiter for ID3D12Fence timeline synchronization.
 * Eliminates synchronous WaitForSingleObject stalls on the presentation render thread.
 */
struct GpuTimelineAwaiter {
    ID3D12Fence* fence = nullptr;
    UINT64 target_value = 0;
    HANDLE wait_handle = nullptr;
    HANDLE event_handle = nullptr;

    constexpr GpuTimelineAwaiter() noexcept = default;
    constexpr GpuTimelineAwaiter(ID3D12Fence* f, UINT64 val, HANDLE wh = nullptr, HANDLE eh = nullptr) noexcept
        : fence(f), target_value(val), wait_handle(wh), event_handle(eh) {}

    [[nodiscard]] bool await_ready() const noexcept {
        if (!fence) return true;
        return fence->GetCompletedValue() >= target_value;
    }

    static void CALLBACK WaitCallback(PVOID context, BOOLEAN /*timed_out*/) noexcept {
        auto* self = static_cast<GpuTimelineAwaiter*>(context);
        // Detach wait_handle before resuming the coroutine because resume() may destroy
        // the coroutine frame (and thus `self`) synchronously on this thread-pool thread.
        // Calling UnregisterWaitEx(..., INVALID_HANDLE_VALUE) from inside the callback itself
        // would deadlock waiting for this very callback to exit.
        HANDLE wh = self->wait_handle;
        self->wait_handle = nullptr;
        if (self->resume_handle) {
            self->resume_handle.resume();
        }
        if (wh) {
            UnregisterWaitEx(wh, nullptr);
        }
    }

    bool await_suspend(std::coroutine_handle<> h) noexcept {
        if (!fence || fence->GetCompletedValue() >= target_value) {
            return false; // Already completed; do not suspend
        }
        resume_handle = h;
        event_handle = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        if (!event_handle) {
            return false;
        }
        if (FAILED(fence->SetEventOnCompletion(target_value, event_handle))) {
            CloseHandle(event_handle);
            event_handle = nullptr;
            return false;
        }
        if (!RegisterWaitForSingleObject(
                &wait_handle, event_handle, &GpuTimelineAwaiter::WaitCallback,
                this, INFINITE, WT_EXECUTEONLYONCE)) {
            CloseHandle(event_handle);
            event_handle = nullptr;
            return false;
        }
        return true;
    }

    void await_resume() noexcept {
        if (event_handle) {
            CloseHandle(event_handle);
            event_handle = nullptr;
        }
    }

    ~GpuTimelineAwaiter() noexcept {
        // If destroyed externally while a wait is still registered (e.g. coroutine cancelled
        // before WaitCallback fired), pass INVALID_HANDLE_VALUE so the OS blocks until any
        // in-flight callback finishes, preventing use-after-free of `this`.
        if (wait_handle) {
            UnregisterWaitEx(wait_handle, INVALID_HANDLE_VALUE);
            wait_handle = nullptr;
        }
        if (event_handle) {
            CloseHandle(event_handle);
            event_handle = nullptr;
        }
    }

private:
    std::coroutine_handle<> resume_handle{};
};

/**
 * Helper to construct a coroutine awaiter for a given D3D12 fence value.
 */
[[nodiscard]] inline GpuTimelineAwaiter wait_for_gpu_fence(ID3D12Fence* fence, UINT64 value) noexcept {
    return GpuTimelineAwaiter{fence, value, nullptr, nullptr};
}

} // namespace librecast::d3d12
