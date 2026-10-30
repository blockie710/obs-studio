/*
 * Copyright (c) 2023 Lain Bailey <lain@obsproject.com>
 * SPDX-FileCopyrightText: Copyright (c) 2026 LibreCast Studio Contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#ifdef __cplusplus
#include <expected>
#endif

struct HRError {
	const char *str;
	HRESULT hr;

	inline constexpr HRError(const char *str, HRESULT hr) noexcept : str(str), hr(hr) {}
};

#if defined(__cplusplus) && (__cplusplus >= 202302L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202004L))
namespace librecast {

enum class GraphicsErrorKind {
	Ok = 0,
	DeviceRemoved,
	DeviceReset,
	OutOfMemory,
	InvalidArg,
	UnknownFailure
};

struct GraphicsError {
	GraphicsErrorKind kind;
	HRESULT hr;
	const char *context;

	static constexpr GraphicsError from_hresult(HRESULT hr, const char *ctx = "") noexcept {
		if (hr == static_cast<HRESULT>(0x887A0005L) /* DXGI_ERROR_DEVICE_REMOVED */)
			return {GraphicsErrorKind::DeviceRemoved, hr, ctx};
		if (hr == static_cast<HRESULT>(0x887A0007L) /* DXGI_ERROR_DEVICE_RESET */)
			return {GraphicsErrorKind::DeviceReset, hr, ctx};
		if (hr == static_cast<HRESULT>(0x8007000EL) /* E_OUTOFMEMORY */)
			return {GraphicsErrorKind::OutOfMemory, hr, ctx};
		if (hr == static_cast<HRESULT>(0x80070057L) /* E_INVALIDARG */)
			return {GraphicsErrorKind::InvalidArg, hr, ctx};
		return {GraphicsErrorKind::UnknownFailure, hr, ctx};
	}
	constexpr bool is_device_removed() const noexcept {
		return kind == GraphicsErrorKind::DeviceRemoved || kind == GraphicsErrorKind::DeviceReset;
	}
};

template <typename T>
using HRResult = std::expected<T, HRError>;

template <typename T>
using GraphicsResult = std::expected<T, GraphicsError>;

} // namespace librecast

namespace util {
using GraphicsErrorKind = librecast::GraphicsErrorKind;
using HRError = librecast::GraphicsError;

template <typename T>
using HRResult = std::expected<T, HRError>;

[[nodiscard]] inline constexpr HRResult<void> check_hr(HRESULT hr, const char *ctx = "") noexcept {
	if (hr < 0) {
		return std::unexpected(HRError::from_hresult(hr, ctx));
	}
	return {};
}
} // namespace util
#endif
