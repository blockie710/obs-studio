# LibreCast Studio Modernization: Phased Implementation Plan & Execution Strategy

**Document Status:** Approved Architectural Strategy  
**Standard Baseline:** C++23 (`/std:c++latest` on MSVC, `-std=c++23` on Clang/GCC)  
**Target Systems:** Presentation Layer, Qt6 Desktop UI, Native DirectX 11/12 Subsystems, and Hybrid Rust Boundaries  
**Date:** 2026-10-29  

---

## 1. Executive Overview & Architectural Objectives

LibreCast Studio is upgrading its core language and standard library baseline from C++17 to **C++23**. This modernization enables high-throughput, low-latency broadcast capabilities across the entire presentation, UI, and graphics pipeline:

1. **Eliminate Presentation Layer Pointer Arithmetic:** Replace raw pointer manipulation and scalar pitch passing across mapped GPU surfaces with type-safe, bounds-checked `std::mdspan` multi-dimensional strided views.
2. **Deterministic, Exception-Free Graphics Subsystems:** Replace `HRError` exception throwing with zero-cost monadic `std::expected<T, GraphicsError>` to ensure real-time rendering loops never execute CPU unwinding tables.
3. **Hardware-Accelerated SIMD Math:** Replace legacy 16-iteration scalar nested loops in `libobs/graphics/matrix4.c` with cross-platform DirectXMath SIMD intrinsics in `basemath.hpp`.
4. **Desktop UI Modernization:** Cleanly integrate Qt 6.11.1 with type-safe `std::format`/`std::println`, native `char8_t` UTF-8 strings, and strict `QT_NO_KEYWORDS` macro hygiene.
5. **Native DirectX 12 & Agility SDK Subsystem:** Deploy a modern D3D12 graphics subsystem utilizing Enhanced Barriers, DirectStorage, and coroutine-based GPU timeline fence pacing.
6. **Encapsulated Hybrid Rust Boundaries:** Preserve pure C ABI compatibility for `bindgen` across upcoming Rust protocol crates while confining C++23 templates to C++ translation units.

---

## 2. Compiler & Toolchain Baseline Specifications

### 2.1 Compiler Matrix
Modernization targets modern toolchains already deployed across engineering workstations and CI runners:

| Toolchain Component | Minimum Version | Target Version | Standard Flag | Notes |
|---|---|---|---|---|
| **Microsoft Visual C++** | 19.44 (VS 2022 17.14) | 19.51 (VS 2026 / 18.11) | `/std:c++latest` | Microsoft gates full C++23 `<expected>`, `<mdspan>`, `<print>`, and `<generator>` behind `/std:c++latest`. |
| **LLVM Clang** | Clang 17.0 | Clang 22.1.8 | `-std=c++23` / `/std:c++latest` | Targets `x86_64-pc-windows-msvc`, auto-discovering MSVC STL. |
| **GCC (Linux)** | GCC 13.1 | GCC 14.2+ | `-std=c++23` | libstdc++ with full C++23 standard library. |
| **Apple Clang (macOS)**| Xcode 15.3 | Xcode 16.0+ | `-std=c++23` | libc++ with full C++23 `<expected>` and ranges. |
| **CMake** | 3.28.0 | 4.4.3 | `CMAKE_CXX_STANDARD 23` | Mandated by `CMakePresets.json`. |
| **Windows SDK** | 10.0.22621.0 | 10.0.26100.0 / 10.0.28000.0 | — | Windows 11 24H2 SDK with DirectXMath v320. |

### 2.2 Global Compiler Flags & Macro Hygiene
In `cmake/common/compiler_common.cmake` and `cmake/windows/compilerconfig.cmake`:
```cmake
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# Enforce macro hygiene and standards conformity globally
add_compile_definitions(
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    QT_NO_KEYWORDS
)

if(MSVC)
    add_compile_options(
        /Zc:__cplusplus   # Ensure __cplusplus reports modern standard version
        /Zc:char8_t-      # Maintain UTF-8 string literal compatibility with legacy C strings
        /utf-8            # UTF-8 source and execution character sets
        /permissive-      # Strict standard conformance
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(
        -fno-char8_t
    )
endif()
```

---

## 3. Phased Migration Milestones

```
Phase 1: Toolchain Baseline & Macro Hygiene
   │
   ▼
Phase 2: Core libobs & Presentation Layer (DirectXMath, std::mdspan, std::expected)
   │
   ▼
Phase 3: Qt6 Desktop UI Modernization (std::format, char8_t, lambda connects)
   │
   ▼
Phase 4: Native DirectX 12 Subsystem & Agility SDK (Enhanced Barriers, Timeline Fences)
   │
   ▼
Phase 5: Zero-Copy Texture Sharing Pipeline (TextureHandle, NVENC/AMF, Rust FFI)
```

---

### Phase 1: Toolchain Baseline, Presets, and Global Macro Hygiene
**Objective:** Upgrade the global build configuration to C++23, enforce macro hygiene across Windows and Qt headers, and verify that all existing modules continue compiling without regressions.

#### Execution Tasks:
1. **Update CMake Global Standard:**
   - In `cmake/common/compiler_common.cmake`, update line 11: `set(CMAKE_CXX_STANDARD 23)`.
   - In `libobs/CMakeLists.txt`, update line 241: `target_compile_features(libobs PUBLIC cxx_std_23)`.
2. **Apply Global Compile Definitions:**
   - Add `NOMINMAX`, `WIN32_LEAN_AND_MEAN`, and `QT_NO_KEYWORDS` to top-level `add_compile_definitions()`.
3. **Deploy Windows Hygiene Header:**
   - Create `shared/util/windows-hygiene.hpp`:
     ```cpp
     #pragma once
     #define NOMINMAX
     #define WIN32_LEAN_AND_MEAN
     #include <windows.h>
     #undef small
     #undef near
     #undef far
     ```
   - Enforce inclusion of `windows-hygiene.hpp` in all Win32-dependent modules to prevent `rpcndr.h` and `minwindef.h` identifier collisions.
4. **Third-Party Submodule Isolation:**
   - Ensure external dependencies (`deps/libdshowcapture`, `plugins/obs-browser`) retain localized `cxx_std_17` target compile features where legacy codebases require it.

**Exit Criteria:** Clean CI build across Windows (MSVC 19.44/19.51 and Clang 22.1.8), macOS, and Linux presets with zero macro collision diagnostics.

---

### Phase 2: Core `libobs` and Presentation Layer Modernization
**Objective:** Replace scalar math loops, manual pointer arithmetic, and exception handling in the core presentation engine with DirectXMath SIMD, `std::mdspan`, and `std::expected`.

#### Execution Tasks:
1. **DirectXMath SIMD Wrappers (`libobs/graphics/basemath.hpp`):**
   - Implement C++ math wrappers utilizing `DirectXMath.h` (version 320).
   - Enforce the unaligned storage vs 16-byte SIMD register pattern:
     - Heap/struct storage: `DirectX::XMFLOAT4X4` and `DirectX::XMFLOAT4`.
     - Computational registers: `DirectX::XMMATRIX` and `DirectX::XMVECTOR`.
   - Replace scalar loop in `libobs/graphics/matrix4.c` (`matrix4_mul`) with `XMMatrixMultiply`, yielding 4x throughput improvement.
2. **`std::mdspan` Strided Texture Mapping:**
   - Modernize `gs_texture_map` and `gs_stagesurface_map` in `libobs-d3d11/d3d11-subsystem.cpp`:
     ```cpp
     template <typename PixelT = uint32_t>
     std::expected<std::mdspan<PixelT, extents_2d, std::layout_stride>, HRESULT>
     gs_stagesurface_map_mdspan(gs_stagesurf_t *stagesurf) {
         D3D11_MAPPED_SUBRESOURCE map;
         HRESULT hr = stagesurf->device->context->Map(stagesurf->texture, 0, D3D11_MAP_READ, 0, &map);
         if (FAILED(hr)) return std::unexpected(hr);
         
         std::layout_stride::mapping<extents_2d> layout{
             extents_2d{stagesurf->height, stagesurf->width},
             std::array<size_t, 2>{map.RowPitch / sizeof(PixelT), 1}
         };
         return std::mdspan<PixelT, extents_2d, std::layout_stride>(
             reinterpret_cast<PixelT*>(map.pData), layout);
     }
     ```
3. **Planar Video Frame Layouts (`NV12FrameView` / `P010FrameView`):**
   - Refactor `video_source_tracker.hpp` to accept structured `NV12FrameView` (Y plane span + UV plane span) instead of 8 scalar arguments.
4. **Monadic Error Propagation:**
   - Deprecate `HRError` exception throwing in `libobs-d3d11`.
   - Refactor graphics device methods to return `std::expected<T, GraphicsError>`, handling `DXGI_ERROR_DEVICE_REMOVED` and `DXGI_ERROR_DEVICE_RESET` explicitly without stack unwinding.
5. **SceneCompositor Layer Iteration:**
   - Introduce `std::generator<const VideoFrame*>` in `SceneCompositor` for zero-allocation layer traversal.

**Exit Criteria:** Zero `HRError` throws in `libobs-d3d11`; all texture mapping utilizes `std::mdspan`; matrix multiplications verify 4x SIMD benchmark speedup.

---

### Phase 3: Qt6 Desktop UI Modernization
**Objective:** Transition the Qt 6.11.1 desktop UI to modern string formatting, type-safe UTF-8 conversions, and robust macro hygiene.

#### Execution Tasks:
1. **String Formatting Modernization:**
   - Migrate legacy `QString::arg()` chains in `frontend/` to `std::format`:
     ```cpp
     // Before:
     QString err = QTStr("About.Error").arg(file).arg(line);
     // After:
     QString err = QString::fromStdString(std::format("Error in {}: line {}", file.toStdString(), line));
     ```
   - Migrate console telemetry from `blog(LOG_ERROR, ...)` to `std::println(stderr, ...)`.
2. **UTF-8 Literals & Qt 6.11.1 Native `char8_t`:**
   - Migrate UI string helpers to leverage `QString::fromUtf8(const char8_t*)` native overloads in `qstring.h`.
3. **Signal & Slot Keyword Hygiene:**
   - Verify all signals/slots in `frontend/` use `Q_SIGNALS`, `Q_SLOTS`, and `Q_EMIT`.
   - Ensure PMF (pointer-to-member-function) and modern lambda connects are used exclusively across `OBSMainWindow`, `OBSBasic`, and dialogs.
4. **Presenter Error Flow:**
   - Return `std::expected<T, UIError>` from asynchronous API helpers (`OBSStudioAPI.cpp`) to prevent unhandled UI error states.

**Exit Criteria:** All Qt UI code compiles warning-free with `QT_NO_KEYWORDS`; zero `QString::arg` calls in performance-sensitive telemetry pathways.

---

### Phase 4: Native DirectX 12 Subsystem & Agility SDK Integration
**Objective:** Implement a greenfield native `libobs-d3d12` graphics subsystem using the DirectX 12 Agility SDK, Enhanced Barriers, and coroutine awaiters for GPU fence pacing.

#### Execution Tasks:
1. **DirectX 12 Agility SDK Distribution:**
   - Integrate Agility SDK 1.614+ via CMake FetchContent or prebuilt package in `.deps`.
   - Export Agility loader symbols in `obs-studio/main.cpp`:
     ```cpp
     extern "C" {
         __declspec(dllexport) extern const UINT D3D12SDKVersion = 614;
         __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
     }
     ```
2. **`libobs-d3d12` Subsystem Architecture:**
   - Create `libobs-d3d12/` implementing `graphics_subsystem` (`device-exports.h`).
   - Implement `D3D12_ENHANCED_BARRIERS` for explicit subresource transitions.
   - Implement Pipeline State Object (PSO) disk cache for compile-free pipeline instantiation.
3. **GPU Timeline Fences & Coroutine Frame Pacing:**
   - Implement `GpuFenceAwaiter` using `ID3D12Fence`:
     ```cpp
     struct GpuTimelineAwaiter {
         ID3D12Fence* fence;
         UINT64 targetValue;
         bool await_ready() const noexcept {
             return fence->GetCompletedValue() >= targetValue;
         }
         void await_suspend(std::coroutine_handle<> h) noexcept {
             HANDLE event = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
             fence->SetEventOnCompletion(targetValue, event);
             RegisterWaitForSingleObject(&waitHandle, event, WaitCallback, h.address(), INFINITE, WT_EXECUTEONLYONCE);
         }
         void await_resume() const noexcept {}
     };
     ```
   - Eliminate synchronous `WaitForSingleObject` thread stalls in the render loop.

**Exit Criteria:** `libobs-d3d12` loads successfully on Windows 10/11; Agility SDK Enhanced Barriers validated in D3D12 debug layer; 60 fps rendering confirmed with 0 thread stalls.

---

### Phase 5: Zero-Copy Texture Sharing Pipeline
**Objective:** Deploy the unified `TextureHandle` specification across graphics backends, hardware encoders (NVENC, AMF, QSV), and the hybrid Rust FFI boundary.

#### Execution Tasks:
1. **Implement `TextureHandle` Core:**
   - Deploy `TextureHandle` struct from `zero-copy-texture-interface-spec.md` into `libobs/graphics/texture-handle.h`.
   - Implement cross-API export/import using NT shared handles (`IDXGIResource1::CreateSharedHandle`, `ID3D12Device::OpenSharedHandle`).
2. **Hardware Encoder Zero-Copy Bridge:**
   - Pass `TextureHandle` directly to NVENC (`NvEncRegisterResource`) and Intel QSV (`mfxFrameSurface1`) without host CPU read-backs.
3. **Hybrid Rust FFI Boundary Encapsulation:**
   - Expose `TextureHandle` as a pure C POD struct in `libobs/obs-ffi.h` (`extern "C"`).
   - Verify `bindgen` generates clean, safe Rust bindings without opaque template bloat.
   - Retain all C++23 template abstractions (`std::expected`, `std::mdspan`) strictly inside C++ translation units.

**Exit Criteria:** End-to-end zero-copy GPU pipeline from `SceneCompositor` to hardware encoder; zero CPU read-back copies; verified `bindgen` compilation on Rust test harness.

---

## 4. Risk Mitigation & Fallback Matrix

| Identified Risk | Severity | Likelihood | Mitigation Strategy | Fallback Option |
|---|---|---|---|---|
| **MSVC `/std:c++latest` ABI Instability** | Medium | Low | LibreCast Studio compiles all plugins and engine binaries from source into a unified distribution artifact. Standard library ABI shifts between minor VS releases do not affect self-contained distributions. | Pin CI build environment to verified VS minor toolset version (e.g. MSVC `14.44.35207`). |
| **Legacy Submodule Incompatibility** | Medium | Medium | Third-party submodules (e.g. `obs-browser`, `libdshowcapture`) may not compile under C++23. Isolate submodules using explicit per-target `target_compile_features(submodule PRIVATE cxx_std_17)`. | Maintain submodules on C++17 while linking against modernized C++23 `libobs` via pure C ABI. |
| **Windows Macro Collisions** | High | High | Legacy Windows macros (`min`, `max`, `small`, `near`, `far`, `interface`) break C++ stdlib and DirectX headers. Enforce `NOMINMAX` globally and `#undef` hygiene after SDK includes via `windows-hygiene.hpp`. | Rename conflicting class/struct members if third-party headers reintroduce macros. |
| **UTF-8 `char8_t` Breaking Changes** | Medium | Medium | C++20/C++23 `u8"..."` literals produce `const char8_t*`, breaking legacy C APIs expecting `const char*`. Pass `/Zc:char8_t-` (MSVC) and `-fno-char8_t` (Clang) globally during the transitional phase. | Explicitly cast literals with `reinterpret_cast<const char*>` in legacy call sites. |
| **Agility SDK Deployment Failures** | Medium | Low | Agility SDK DLLs (`D3D12Core.dll`) missing on target client machines. Bundle Agility SDK binaries in `bin/64bit/D3D12/` subfolder alongside application executable. | Fallback to OS-provided native D3D12 runtime or D3D11 subsystem if Agility loader fails. |

---

## 5. Quality Assurance, Test Gates & Verification Strategy

### 5.1 Verification Commands
Every phase must execute and pass the automated test suites prior to milestone sign-off:

1. **Compilation Spikes Validation:**
   ```powershell
   pwsh -ExecutionPolicy Bypass -File "spikes/cpp20_vs_cpp23/run_spikes.ps1"
   ```
   *Requirement:* All 12 spike tests must pass (12/12 SUCCESS), logging results to `spikes/cpp20_vs_cpp23/SPIKE_RESULTS.md`.

2. **CMake Configure & Build Check (RelWithDebInfo):**
   ```powershell
   cmake --preset windows-x64
   cmake --build --preset windows-x64 --config RelWithDebInfo
   ```

3. **Presentation Frame Pacing Telemetry Check:**
   - Execute benchmark harness verifying that `SceneCompositor` frame delivery adheres to $16,700\ \mu\text{s} \pm 200\ \mu\text{s}$ under full 32-layer load.

---

## 6. Implementation Timeline & Roadmap

```
+-------------------------------------------------------------+
| Milestone | Description                          | Duration |
+-------------------------------------------------------------+
| Phase 1   | Toolchain Baseline & Macro Hygiene   | Week 1   |
| Phase 2   | Core libobs & Presentation Layer     | Week 2-3 |
| Phase 3   | Qt6 Desktop UI Modernization         | Week 4   |
| Phase 4   | Native D3D12 & Agility SDK Subsystem | Week 5-6 |
| Phase 5   | Zero-Copy Texture Sharing & FFI      | Week 7   |
+-------------------------------------------------------------+
```
