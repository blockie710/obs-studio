/******************************************************************************
    Copyright (C) 2026 LibreCast Studio Contributors

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.
******************************************************************************/

#pragma once

/**
 * @file d3d12-subsystem.hpp
 * @brief Phase 4 Native DirectX 12 Subsystem:
 *  1. DirectX 12 Agility SDK 1.614+ configuration constants.
 *  2. C++20/23 Coroutine GPU Timeline Fence Awaiter (`GpuTimelineAwaiter`).
 *  3. DirectX 12 Enhanced Barriers (`D3D12_TEXTURE_BARRIER` / subresource transitions)
 *     replacing legacy `D3D12_RESOURCE_BARRIER` state tracking.
 *  4. Pipeline State Object (PSO) Disk Cache serializer with `std::expected<T, HRError>`.
 *  5. Zero-copy DXGI shared NT handle bridge (`nexus::TextureHandle`).
 */

#include "d3d12-fence-awaiter.hpp"
#include "../libobs/graphics/texture-handle.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

namespace obs::d3d12 {

inline constexpr uint32_t kAgilitySDKVersion = 614;
inline constexpr std::string_view kAgilitySDKPath = ".\\D3D12\\";

/**
 * @brief DirectX 12 Enhanced Barrier Sync / Access / Layout flags (Agility SDK 1.614+)
 * Provides portable strongly-typed descriptors even when compiling against older Windows SDK headers.
 */
enum class BarrierSync : uint32_t {
	None = 0x0,
	All = 0x1,
	Draw = 0x2,
	RenderTarget = 0x400,
	ComputeShading = 0x2000,
	PixelShading = 0x200,
	Copy = 0x100000,
	VideoEncode = 0x4000000,
};

enum class BarrierAccess : uint32_t {
	Common = 0,
	RenderTarget = 0x4,
	UnorderedAccess = 0x8,
	ShaderResource = 0x200,
	CopyDest = 0x400,
	CopySource = 0x800,
	VideoEncodeRead = 0x80000,
	NoAccess = 0x80000000u,
};

enum class BarrierLayout : uint32_t {
	Undefined = 0xFFFFFFFFu,
	Common = 0,
	Present = 0,
	GenericRead = 1,
	RenderTarget = 2,
	UnorderedAccess = 3,
	ShaderResource = 4,
	CopyDest = 5,
	CopySource = 6,
	VideoEncodeRead = 16,
};

struct EnhancedSubresourceRange {
	uint32_t index_or_first_mip = 0xFFFFFFFFu;
	uint32_t num_mip_levels = 0;
	uint32_t first_array_slice = 0;
	uint32_t num_array_slices = 0;
	uint32_t first_plane = 0;
	uint32_t num_planes = 0;
};

struct EnhancedTextureBarrierDesc {
	BarrierSync sync_before = BarrierSync::None;
	BarrierSync sync_after = BarrierSync::None;
	BarrierAccess access_before = BarrierAccess::Common;
	BarrierAccess access_after = BarrierAccess::Common;
	BarrierLayout layout_before = BarrierLayout::Undefined;
	BarrierLayout layout_after = BarrierLayout::Common;
	void *resource = nullptr;
	EnhancedSubresourceRange subresources{};
	uint32_t flags = 0;
};

/**
 * @brief Constructs an Enhanced Texture Barrier transitioning a compositor render target
 * directly to a zero-copy shader resource / video encode read layout without legacy state decay.
 */
[[nodiscard]] constexpr EnhancedTextureBarrierDesc make_render_target_to_encode_barrier(
	void *resource, uint32_t plane_slice = 0) noexcept
{
	return EnhancedTextureBarrierDesc{
		.sync_before = BarrierSync::RenderTarget,
		.sync_after = BarrierSync::VideoEncode,
		.access_before = BarrierAccess::RenderTarget,
		.access_after = BarrierAccess::VideoEncodeRead,
		.layout_before = BarrierLayout::RenderTarget,
		.layout_after = BarrierLayout::VideoEncodeRead,
		.resource = resource,
		.subresources = EnhancedSubresourceRange{
			.index_or_first_mip = 0,
			.num_mip_levels = 1,
			.first_array_slice = 0,
			.num_array_slices = 1,
			.first_plane = plane_slice,
			.num_planes = 1,
		},
		.flags = 0,
	};
}

/**
 * @brief Pipeline State Object (PSO) Disk Cache binary header and serializer.
 * Eliminates shader compilation stutter on startup by validating adapter LUID + SDK version.
 */
struct PsoCacheHeader {
	uint32_t magic = 0x3231434Cu; // 'LC12'
	uint32_t agility_sdk_version = kAgilitySDKVersion;
	uint64_t adapter_luid = 0;
	uint32_t entry_count = 0;
	uint32_t payload_bytes = 0;
};
static_assert(sizeof(PsoCacheHeader) == 24, "PsoCacheHeader must be tightly packed to 24 bytes");

class PsoDiskCache {
public:
	explicit constexpr PsoDiskCache(uint64_t adapter_luid) noexcept : adapter_luid_(adapter_luid) {}

	[[nodiscard]] std::expected<std::vector<uint8_t>, util::HRError>
	serialize_blob(std::span<const uint8_t> cached_pso_blob) const
	{
		if (cached_pso_blob.empty()) {
			return std::unexpected(util::HRError::from_hresult(E_INVALIDARG, "PSO cache blob cannot be empty"));
		}

		PsoCacheHeader header{
			.magic = 0x3231434Cu,
			.agility_sdk_version = kAgilitySDKVersion,
			.adapter_luid = adapter_luid_,
			.entry_count = 1,
			.payload_bytes = static_cast<uint32_t>(cached_pso_blob.size()),
		};

		std::vector<uint8_t> out(sizeof(PsoCacheHeader) + cached_pso_blob.size());
		std::memcpy(out.data(), &header, sizeof(PsoCacheHeader));
		std::memcpy(out.data() + sizeof(PsoCacheHeader), cached_pso_blob.data(), cached_pso_blob.size());
		return out;
	}

	[[nodiscard]] std::expected<std::span<const uint8_t>, util::HRError>
	validate_and_extract(std::span<const uint8_t> disk_bytes) const noexcept
	{
		if (disk_bytes.size() <= sizeof(PsoCacheHeader)) {
			return std::unexpected(util::HRError::from_hresult(E_INVALIDARG, "Truncated PSO disk cache header"));
		}

		PsoCacheHeader header{};
		std::memcpy(&header, disk_bytes.data(), sizeof(PsoCacheHeader));
		if (header.magic != 0x3231434Cu || header.agility_sdk_version != kAgilitySDKVersion) {
			return std::unexpected(util::HRError::from_hresult(E_FAIL, "PSO cache version or magic mismatch"));
		}
		if (header.adapter_luid != adapter_luid_) {
			return std::unexpected(util::HRError::from_hresult(DXGI_ERROR_DEVICE_REMOVED, "Adapter LUID changed; invalidating PSO cache"));
		}
		if (sizeof(PsoCacheHeader) + header.payload_bytes != disk_bytes.size()) {
			return std::unexpected(util::HRError::from_hresult(E_FAIL, "PSO cache payload size mismatch"));
		}

		return disk_bytes.subspan(sizeof(PsoCacheHeader), header.payload_bytes);
	}

private:
	uint64_t adapter_luid_ = 0;
};

/**
 * @brief Exports a D3D12 shared NT handle into a zero-copy `lc_texture_handle_t` descriptor.
 */
[[nodiscard]] inline std::expected<lc_texture_handle_t, util::HRError>
export_shared_texture_handle(uintptr_t nt_shared_handle, uint32_t width, uint32_t height,
			     lc_pixel_format_t format, uint64_t pts_us, uint32_t frame_seq) noexcept
{
	if (nt_shared_handle == 0 || width == 0 || height == 0) {
		return std::unexpected(util::HRError::from_hresult(E_INVALIDARG, "Invalid D3D12 shared texture parameters"));
	}

	lc_texture_handle_t handle{};
	handle.native_handle = nt_shared_handle;
	handle.pts_us = pts_us;
	handle.width = width;
	handle.height = height;
	handle.frame_seq = frame_seq;
	handle.shared_fd = -1;
	handle.api = static_cast<uint8_t>(LC_GRAPHICS_API_D3D12);
	handle.format = static_cast<uint8_t>(format);
	return handle;
}

} // namespace obs::d3d12
