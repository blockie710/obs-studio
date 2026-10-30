/******************************************************************************
    Copyright (C) 2026 LibreCast Studio Contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

/**
 * @file obs-ffi.h
 * @brief Pure C ABI boundary header for Rust `bindgen` & `cxx` interop (Phase 5).
 *
 * CRITICAL RULE:
 *   This header MUST remain strictly C17/C99 compatible (`extern "C"`) and MUST NOT
 *   expose C++23 STL types (`std::expected`, `std::mdspan`, `std::generator`, `std::span`)
 *   outside `#ifdef __cplusplus` implementation blocks, ensuring `bindgen` can parse
 *   all zero-copy texture descriptors and FFI entry points without template instantiation errors.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "graphics/texture-handle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum lc_ffi_status {
	LC_FFI_OK = 0,
	LC_FFI_ERR_NULL_POINTER = -1,
	LC_FFI_ERR_INVALID_HANDLE = -2,
	LC_FFI_ERR_INVALID_DIMENSIONS = -3,
	LC_FFI_ERR_UNSUPPORTED_API = -4,
	LC_FFI_ERR_PIPELINE_DISCONNECTED = -5,
} lc_ffi_status_t;

/**
 * @brief Validates a `lc_texture_handle_t` across the C/Rust FFI boundary.
 */
static inline lc_ffi_status_t obs_ffi_validate_texture_handle(const lc_texture_handle_t *handle)
{
	if (!handle) {
		return LC_FFI_ERR_NULL_POINTER;
	}
	if (handle->native_handle == 0 && handle->shared_fd <= 0) {
		return LC_FFI_ERR_INVALID_HANDLE;
	}
	if (handle->width == 0 || handle->height == 0) {
		return LC_FFI_ERR_INVALID_DIMENSIONS;
	}
	if (handle->api > LC_GRAPHICS_API_OPENGL) {
		return LC_FFI_ERR_UNSUPPORTED_API;
	}
	return LC_FFI_OK;
}

/**
 * @brief Callback signature for zero-copy texture consumers across the FFI boundary (e.g., Rust WebRTC/QUIC transport).
 */
typedef lc_ffi_status_t (*obs_ffi_texture_sink_cb)(const lc_texture_handle_t *handle, void *user_data);

/**
 * @brief Dispatches a zero-copy GPU texture handle to a registered FFI sink after validation.
 */
static inline lc_ffi_status_t obs_ffi_submit_texture(const lc_texture_handle_t *handle,
						     obs_ffi_texture_sink_cb sink_cb, void *user_data)
{
	lc_ffi_status_t status = obs_ffi_validate_texture_handle(handle);
	if (status != LC_FFI_OK) {
		return status;
	}
	if (!sink_cb) {
		return LC_FFI_ERR_NULL_POINTER;
	}
	return sink_cb(handle, user_data);
}

#ifdef __cplusplus
}
#endif
