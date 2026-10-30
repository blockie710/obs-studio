/******************************************************************************
    SPDX-FileCopyrightText: Copyright (c) 2026 LibreCast Studio Contributors
    SPDX-License-Identifier: GPL-3.0-or-later

    Phase 3: Qt6 Modern String Formatting, UTF-8, & Monadic Presenter Helpers
    Location: frontend/utility/qt6-modern-helpers.hpp
******************************************************************************/

#pragma once

#include "../../shared/util/windows-hygiene.hpp"

#include <cstdio>
#include <expected>
#include <format>
#include <string>
#include <string_view>

#if __has_include(<print>) && (__cplusplus >= 202302L || (defined(_MSVC_LANG) && _MSVC_LANG >= 202004L))
#include <print>
#define LIBRECAST_HAS_STD_PRINT 1
#endif

#include <QString>

namespace librecast::ui {

enum class UIErrorCode {
    None = 0,
    InvalidParameter,
    InvalidSourceHandle,
    BackendDisconnected,
    ResourceUnavailable,
    ProfileLoadFailed,
    SceneCollectionFailed,
    EncoderInitFailed
};

struct UIError {
    UIErrorCode code = UIErrorCode::None;
    std::string message;

    static UIError InvalidParameter(std::string msg = "Invalid parameter") {
        return UIError{UIErrorCode::InvalidParameter, std::move(msg)};
    }
    static UIError InvalidSourceHandle(std::string msg = "Invalid OBS source handle") {
        return UIError{UIErrorCode::InvalidSourceHandle, std::move(msg)};
    }
    static UIError BackendDisconnected(std::string msg = "OBS backend disconnected") {
        return UIError{UIErrorCode::BackendDisconnected, std::move(msg)};
    }
    static UIError ProfileLoadFailed(std::string msg = "Profile or scene collection load failed") {
        return UIError{UIErrorCode::ProfileLoadFailed, std::move(msg)};
    }
};

[[nodiscard]] inline std::string_view to_string(const UIError& err) noexcept {
    if (!err.message.empty()) {
        return err.message;
    }
    switch (err.code) {
    case UIErrorCode::None: return "No error";
    case UIErrorCode::InvalidParameter: return "Invalid parameter";
    case UIErrorCode::InvalidSourceHandle: return "Invalid OBS source handle";
    case UIErrorCode::BackendDisconnected: return "OBS backend disconnected";
    case UIErrorCode::ResourceUnavailable: return "Resource unavailable";
    case UIErrorCode::ProfileLoadFailed: return "Profile load failed";
    case UIErrorCode::SceneCollectionFailed: return "Scene collection failed";
    case UIErrorCode::EncoderInitFailed: return "Encoder initialization failed";
    }
    return "Unknown UI error";
}

template <typename T>
using UIResult = std::expected<T, UIError>;

/**
 * Type-safe C++23 std::format wrapper producing a QString directly without
 * chained QString::arg() allocations.
 */
template <typename... Args>
[[nodiscard]] inline QString formatQString(std::format_string<Args...> fmt, Args&&... args) {
    const std::string formatted = std::format(fmt, std::forward<Args>(args)...);
    return QString::fromUtf8(formatted.data(), static_cast<qsizetype>(formatted.size()));
}

/**
 * Direct unbuffered C++23 std::println console telemetry helper.
 */
template <typename... Args>
inline void logTelemetryError(std::format_string<Args...> fmt, Args&&... args) {
#if defined(LIBRECAST_HAS_STD_PRINT)
    std::println(stderr, fmt, std::forward<Args>(args)...);
#else
    const std::string msg = std::format(fmt, std::forward<Args>(args)...);
    std::fprintf(stderr, "%s\n", msg.c_str());
#endif
}

/**
 * Zero-overhead UTF-8 string view to QString conversion helper.
 */
[[nodiscard]] inline QString toQString(std::string_view utf8) {
    return QString::fromUtf8(utf8.data(), static_cast<qsizetype>(utf8.size()));
}

#if defined(__cpp_char8_t)
/**
 * Native char8_t UTF-8 literal overload for Qt 6.11+.
 */
[[nodiscard]] inline QString toQString(std::u8string_view u8str) {
    return QString::fromUtf8(reinterpret_cast<const char*>(u8str.data()),
                             static_cast<qsizetype>(u8str.size()));
}
#endif

} // namespace librecast::ui
