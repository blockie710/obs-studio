/**
 * LibreCast Studio - Windows Macro Hygiene Envelope
 * Location: shared/util/windows-hygiene.hpp
 * 
 * Provides an isolated, hygienic inclusion wrapper for Windows SDK headers,
 * preventing legacy Win32 macro pollution (min, max, small, near, far,
 * interface, ERROR, RGB) from corrupting the modern C++ standard library,
 * Qt6 headers, and DirectX 11/12 pipelines.
 */

#pragma once

#if defined(_WIN32) || defined(_WIN64)

// ============================================================================
// 1. Pre-inclusion configuration defines
// ============================================================================

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef STRICT
#define STRICT
#endif

#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

// Prevent windows.h from including winsock.h (preventing Winsock 1 vs 2 conflict)
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

// ============================================================================
// 2. Core Windows and COM/RPC Header Inclusions
// Include unknwn.h and objbase.h here so subsequent includes cannot re-inject
// 'small' or 'interface' macros after our hygiene sweep.
// ============================================================================

#include <windows.h>
#include <unknwn.h>
#include <objbase.h>
#ifndef DEFINE_GUID
#define DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
    EXTERN_C const GUID DECLSPEC_SELECTANY name = { l, w1, w2, { b1, b2, b3, b4, b5, b6, b7, b8 } }
#endif

// ============================================================================
// 3. Post-inclusion Macro Hygiene Sweep
// Undefine identifier-colliding macros injected by minwindef.h, rpcndr.h,
// basetyps.h, and wingdi.h.
// ============================================================================

// min / max: Clashes with std::min, std::max, std::numeric_limits<T>::max()
#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

// small: rpcndr.h defines '#define small char', breaking struct fields and enums
#ifdef small
#undef small
#endif

// near / far: 16-bit segmented relics in minwindef.h, silently erasing clipping plane identifiers.
// Note: minwindef.h defines `#define FAR far` and `#define NEAR near`. Re-anchoring `FAR` and `NEAR`
// directly to empty tokens ensures subsequent COM/DirectX headers (<unknwn.h>, <objbase.h>, <d3d12.h>)
// using `FAR *` compile cleanly even after `near` and `far` are undefined.
#ifdef near
#undef near
#endif

#ifdef far
#undef far
#endif

#ifdef FAR
#undef FAR
#endif
#define FAR

#ifdef NEAR
#undef NEAR
#endif
#define NEAR

// interface: basetyps.h defines '#define interface struct', clashing with C++ class/variable names
#ifdef interface
#undef interface
#endif

// ERROR: wingdi.h defines '#define ERROR 0', clashing with log levels and std::expected error codes
#ifdef ERROR
#undef ERROR
#endif

// RGB: wingdi.h defines '#define RGB(r,g,b)', clashing with color format enum constants
#ifdef RGB
#undef RGB
#endif

#endif // defined(_WIN32) || defined(_WIN64)
