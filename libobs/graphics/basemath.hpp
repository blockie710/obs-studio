/******************************************************************************
    SPDX-FileCopyrightText: Copyright (c) 2026 LibreCast Studio Contributors
    SPDX-License-Identifier: GPL-3.0-or-later

    Phase 2: DirectXMath SIMD Wrapper & Unaligned Storage Adapter
    Location: libobs/graphics/basemath.hpp
******************************************************************************/

#pragma once

#include <cstdint>
#include "matrix4.h"
#include "vec3.h"
#include "vec4.h"

#if defined(_WIN32) || defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <DirectXMath.h>
#endif

namespace librecast::math {

/**
 * Unaligned 4x4 matrix storage layout compatible with struct matrix4 and DirectX::XMFLOAT4X4,
 * paired with explicit 16-byte SIMD register load/store conversion helpers.
 */
struct alignas(16) SimdMatrix4 {
    struct matrix4 raw;

#if defined(_WIN32) || defined(_WIN64)
    [[nodiscard]] inline DirectX::XMMATRIX load() const noexcept {
        return DirectX::XMMATRIX(
            DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&raw.x)),
            DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&raw.y)),
            DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&raw.z)),
            DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&raw.t))
        );
    }

    inline void store(DirectX::FXMMATRIX mat) noexcept {
        DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(&raw.x), mat.r[0]);
        DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(&raw.y), mat.r[1]);
        DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(&raw.z), mat.r[2]);
        DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(&raw.t), mat.r[3]);
    }
#endif
};

/**
 * High-throughput SIMD 4x4 matrix multiplication operating directly on OBS matrix4 structs.
 * Guarantees safe unaligned loads/stores even when struct matrix4 resides on unaligned heap blocks.
 */
inline void multiply_matrix4_simd(struct matrix4* dst, const struct matrix4* m1, const struct matrix4* m2) noexcept {
#if defined(_WIN32) || defined(_WIN64)
    const DirectX::XMMATRIX xm1(
        DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&m1->x)),
        DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&m1->y)),
        DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&m1->z)),
        DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&m1->t))
    );
    const DirectX::XMMATRIX xm2(
        DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&m2->x)),
        DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&m2->y)),
        DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&m2->z)),
        DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&m2->t))
    );
    const DirectX::XMMATRIX res = DirectX::XMMatrixMultiply(xm1, xm2);
    DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(&dst->x), res.r[0]);
    DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(&dst->y), res.r[1]);
    DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(&dst->z), res.r[2]);
    DirectX::XMStoreFloat4(reinterpret_cast<DirectX::XMFLOAT4*>(&dst->t), res.r[3]);
#else
    matrix4_mul(dst, m1, m2);
#endif
}

/**
 * High-throughput SIMD determinant calculation for struct matrix4.
 */
[[nodiscard]] inline float determinant_matrix4_simd(const struct matrix4* m) noexcept {
#if defined(_WIN32) || defined(_WIN64)
    const DirectX::XMMATRIX xm(
        DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&m->x)),
        DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&m->y)),
        DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&m->z)),
        DirectX::XMLoadFloat4(reinterpret_cast<const DirectX::XMFLOAT4*>(&m->t))
    );
    return DirectX::XMVectorGetX(DirectX::XMMatrixDeterminant(xm));
#else
    return matrix4_determinant(m);
#endif
}

} // namespace librecast::math
