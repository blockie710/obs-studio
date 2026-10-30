/******************************************************************************
    SPDX-FileCopyrightText: Copyright (c) 2026 LibreCast Studio Contributors
    SPDX-License-Identifier: GPL-3.0-or-later

    Phase 5: Unified Zero-Copy Texture Handle & Pure C FFI Contract
    Location: libobs/graphics/texture-handle.h
******************************************************************************/

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
#include <type_traits>
extern "C" {
#endif

/**
 * Identifies the underlying native graphics API owning a texture resource.
 */
typedef enum lc_graphics_api {
    LC_GRAPHICS_API_VULKAN    = 0,
    LC_GRAPHICS_API_DIRECTX12 = 1,
    LC_GRAPHICS_API_D3D12     = 1,
    LC_GRAPHICS_API_DIRECTX11 = 2,
    LC_GRAPHICS_API_D3D11     = 2,
    LC_GRAPHICS_API_METAL     = 3,
    LC_GRAPHICS_API_OPENGL    = 4,
    LC_GRAPHICS_API_UNKNOWN   = 0xFF
} lc_graphics_api_t;

/**
 * Identifies the underlying GPU pixel format for zero-copy texture sharing.
 */
typedef enum lc_pixel_format {
    LC_PIXEL_FORMAT_UNKNOWN = 0,
    LC_PIXEL_FORMAT_NV12    = 1,
    LC_PIXEL_FORMAT_P010    = 2,
    LC_PIXEL_FORMAT_I420    = 3,
    LC_PIXEL_FORMAT_RGBA8   = 4,
    LC_PIXEL_FORMAT_BGRA8   = 5
} lc_pixel_format_t;

/**
 * Pure C POD zero-copy GPU texture handle (32 bytes aligned) designed for
 * lock-free SPSC transfer between the compositor, hardware encoders (NVENC,
 * AMF, QSV), and the Rust FFI boundary (`bindgen` / `cxx`).
 */
typedef struct lc_texture_handle {
    uint64_t native_handle; /**< GPU texture pointer/handle (ID3D12Resource*, ID3D11Texture2D*, VkImage) */
    uint64_t pts_us;        /**< Monotonic presentation timestamp in microseconds */
    uint32_t width;         /**< Texture width in pixels */
    uint32_t height;        /**< Texture height in pixels */
    uint32_t frame_seq;     /**< Monotonic sequence number for stale-frame detection */
    int16_t  shared_fd;     /**< Optional DMABUF fd or index (-1 if unused) */
    uint8_t  api;           /**< lc_graphics_api_t value */
    uint8_t  format;        /**< Pixel format identifier (e.g. NV12, P010, RGBA) */
} lc_texture_handle_t;

#ifdef __cplusplus
} // extern "C"

namespace nexus {

using GraphicsAPI = lc_graphics_api_t;
using TextureHandle = lc_texture_handle_t;

static_assert(sizeof(TextureHandle) == 32, "TextureHandle must be exactly 32 bytes for cache-line and FFI alignment");
static_assert(std::is_trivially_copyable_v<TextureHandle>, "TextureHandle must be trivially copyable for lock-free SPSC queues");
static_assert(std::is_standard_layout_v<TextureHandle>, "TextureHandle must have standard layout for Rust FFI compatibility");

} // namespace nexus
#endif
