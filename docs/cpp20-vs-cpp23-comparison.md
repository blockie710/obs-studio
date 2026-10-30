# Technical Comparison: C++20 vs C++23 for LibreCast Studio Modernization

**Author:** Modernization Implementation Working Group  
**Target Architecture:** LibreCast Studio Presentation Layer, Qt6 Desktop UI, Native DirectX 11/12 Subsystems, and Hybrid Rust Boundaries  
**Status:** Canonical Technical Evaluation & Recommendation  
**Date:** 2026-10-29  

---

## 1. Executive Summary & Strategic Recommendation

### 1.1 Context
LibreCast Studio currently operates on a C17/C++17 global baseline across `cmake/common/compiler_common.cmake` and `libobs/CMakeLists.txt`. While this baseline supported the initial fork from OBS Studio, it imposes significant technical debt on high-performance broadcast pathways:
1. **Presentation Layer Stride Overhead:** Mapped texture buffers (`gs_texture_map`, `gs_stagesurface_map`) and video frame planar layouts (`NV12`, `P010`, `I420`) rely on manual scalar pointer arithmetic (`data + y * linesize + x * bpp`), passing up to eight independent parameters per frame source.
2. **Exception Overhead in Real-Time Pipelines:** Graphics subsystems (`libobs-d3d11`) throw `HRError` exceptions across C++/COM boundaries during device removal and shader compilation, violating real-time video/audio zero-exception execution requirements.
3. **Suboptimal Vector Math:** 4x4 matrix transforms in `libobs/graphics/matrix4.c` execute 16-iteration scalar nested loops instead of leveraging 4-wide SIMD instructions.
4. **Desktop UI Friction:** Qt6 strings rely on legacy `QString::arg()` formatting chains and legacy UTF-8 conversions.

### 1.2 Explicit Recommendation: Adopt C++23 as the Project Modernization Baseline
Based on empirical compilation proofs, toolchain maturity benchmarks on developer workstations, and architectural requirements:
- **Primary Recommendation:** **Adopt C++23 (`CMAKE_CXX_STANDARD 23` translating to `/std:c++latest` on MSVC and `-std=c++23` on Clang/GCC) as the canonical language baseline for LibreCast Studio's presentation layer, graphics pipelines, and modernized core.**
- **Transitional Flag Policy:** Retain `/Zc:char8_t-` (MSVC) and `-fno-char8_t` (Clang) globally to maintain UTF-8 literal compatibility with legacy C strings across external submodules while allowing modernized modules to utilize native `char8_t` overloads in Qt 6.11.1.
- **FFI Boundary Encapsulation:** Enforce a strict architectural boundary encapsulating C++23 templates (`std::expected`, `std::mdspan`, `std::generator`) strictly within C++ translation units, preserving pure C ABI (`extern "C"`) and POD structures across the hybrid C++/Rust boundary.

### 1.3 Key Architectural Differentiators
| Feature Area | C++20 Baseline | C++23 Modernization | Architectural Impact on LibreCast Studio |
|---|---|---|---|
| **Multi-dimensional Layouts** | `std::span<T>` (1D contiguous only) | `std::mdspan<T, Extents, LayoutPolicy>` (`<mdspan>`, P0009R18) with `operator[y, x]` | **Transformative.** Eliminates manual pitch and offset calculation across texture maps, scene composition, and YUV planar formats. Zero runtime overhead. |
| **Error Handling** | C++ Exceptions (`HRError`), boolean codes, or out-parameters | `std::expected<T, E>` (`<expected>`, P0323R12) with monadic `.and_then()` | **Critical.** Eliminates exception table overhead in real-time graphics and compositor threads. Strongly forces explicit handling of DXGI device removal. |
| **Console & Crash Logging** | `std::format` (requires heap string allocation for output) | `std::print` and `std::println` (`<print>`, P2093R14) | **High.** Direct unbuffered Unicode output to standard error/console without intermediate string copies or stream locks. |
| **Multi-Track Scene Compositing** | Manual index-based for loops | `std::views::zip`, `std::views::chunk`, `std::ranges::to` | **Moderate.** Declarative iteration across parallel compositor layers, audio clocks, and transform matrices. |
| **Class Builders & CRTP** | Verbose template CRTP inheritance | Deducing `this` / Explicit object parameter (P0847R7) | **Moderate.** Simplifies graphics resource wrappers, PSO builder patterns, and method chaining. |

---

## 2. Presentation Layer Modernization Evaluation

### 2.1 Memory Layouts: `std::span` (1D) vs `std::mdspan` (Multi-Dimensional Strided Layouts)
In LibreCast Studio's presentation layer (`SceneCompositor` and `libobs/obs-video.c`), video frames and GPU surfaces possess 2D and 3D memory layouts with hardware-aligned row pitches (strides). 

#### Current State (Legacy Pointer Math)
Mapped surfaces in `libobs-d3d11/d3d11-subsystem.cpp` (`gs_stagesurface_map`) return raw untyped pointers and scalar pitches:
```cpp
bool gs_stagesurface_map(gs_stagesurf_t *stagesurf, uint8_t **data, uint32_t *linesize);
```
Consumers in `video_source_tracker.hpp` must pass up to eight scalar parameters:
```cpp
virtual bool getFrame(uint8_t* yPlane, uint8_t* uPlane, uint8_t* vPlane,
                      int width, int height, int yStride, int uStride, int vStride) = 0;
```
Accessing pixel $(x, y)$ requires manual index computation:
```cpp
uint8_t* pixel = yPlane + (y * yStride) + x;
```
This pattern is prone to buffer overflows, integer multiplication bugs, and off-by-one errors when processing chroma subsampling.

#### C++20 Limitation (`std::span`)
In C++20, `std::span<T>` represents only a 1D contiguous range. Slicing a 2D image requires either:
1. Creating an array of 1D spans (one per scanline), introducing allocation or struct overhead.
2. Indexing a 1D span manually via `span[y * stride + x]`, failing to provide multidimensional bounds safety or layout abstraction.

#### C++23 Advantage (`std::mdspan`)
C++23 introduces `std::mdspan` (P0009R18), multidimensional subscripting `operator[y, x]` (P2128R6), and layout policies including `std::layout_stride` and `std::layout_right`.

For video frames mapped from DXGI staging textures:
```cpp
using extents_2d = std::extents<size_t, std::dynamic_extent, std::dynamic_extent>;

// Row-strided mapping for GPU texture pitch
std::layout_stride::mapping<extents_2d> layout{
    extents_2d{height, width},
    std::array<size_t, 2>{linesize / sizeof(PixelT), 1}
};

std::mdspan<PixelT, extents_2d, std::layout_stride> frameView(ptr, layout);

// Natural, bounds-safe multidimensional access:
PixelT pixel = frameView[y, x];
```

#### Planar Video Layouts (NV12, P010, I420)
For semi-planar formats like `NV12` (Y plane followed by interleaved UV plane) or `P010` (10-bit NV12):
```cpp
struct NV12FrameView {
    std::mdspan<uint8_t, extents_2d, std::layout_stride> y_plane;
    std::mdspan<uint16_t, extents_2d, std::layout_stride> uv_plane; // Interleaved U+V as 16-bit word
};
```
Slicing regions of interest (ROI) for canvas picture-in-picture transformations is natively supported via `std::submdspan` (P2630R4) without copying underlying memory.

### 2.2 Error Handling: `std::expected` vs Exceptions (`HRError`) and Return Codes
Real-time presentation threads must operate under deterministic latency. Frame presentation occurs at $16.6\text{ ms}$ intervals for 60 fps broadcast.

#### Failures of the Current Model
1. **Exception Table Overhead:** `libobs-d3d11` utilizes `HRError` (`HRError.hpp`), throwing exceptions across COM call boundaries. In MSVC, exception handling registers unwind tables (`__CxxFrameHandler4`), inhibits aggressive compiler register allocation across function boundaries, and incurs significant CPU cache churn during unwinding.
2. **Boolean Return Codes:** Methods returning `bool` lose error context (`HRESULT`, DXGI removal reasons), forcing logging to occur at the error site rather than delegating recovery decisions to the caller.

#### The C++23 `std::expected<T, E>` Paradigm
C++23 `<expected>` (P0323R12) introduces value-or-error vocabulary types with monadic operations (`.and_then()`, `.transform()`, `.or_else()`):
```cpp
enum class DeviceStatus : uint32_t {
    DeviceRemoved,
    DeviceReset,
    DriverInternalError,
    OutOfMemory
};

std::expected<ComPtr<ID3D11Texture2D>, DeviceStatus> createStagingSurface(uint32_t width, uint32_t height) {
    D3D11_TEXTURE2D_DESC desc{};
    // ... configure desc ...
    ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = device->CreateTexture2D(&desc, nullptr, tex.Assign());
    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_DEVICE_REMOVED) return std::unexpected(DeviceStatus::DeviceRemoved);
        if (hr == DXGI_ERROR_DEVICE_RESET) return std::unexpected(DeviceStatus::DeviceReset);
        return std::unexpected(DeviceStatus::DriverInternalError);
    }
    return tex;
}
```
**Benefits:**
- **Zero Allocations & Zero Unwind Tables:** Compiles into standard register-based discriminant flags.
- **Monadic Chaining:**
  ```cpp
  auto surface = createStagingSurface(1920, 1080)
      .and_then([](auto tex) { return mapTexture(tex); })
      .transform([](auto mapped) { return processFrame(mapped); });
  ```

### 2.3 Coroutines & Presentation Layer Synchronization
`SceneCompositor` (`src/nexus/SceneCompositor.hpp`) coordinates a 32-layer slot pool (`kMaxLayers = 32`) aligned to audio clocks with a frame budget of $16,700\ \mu\text{s}$.

#### Current Pacing Mechanism
In `libobs-d3d11/d3d11-subsystem.cpp` (lines 2306–2318), presentation readiness polls the DXGI waitable object with a 0ms synchronous timeout:
```cpp
bool device_is_present_ready(gs_device_t *device) {
    const HANDLE hWaitable = curSwapChain->hWaitable;
    return (hWaitable == NULL) || WaitForSingleObject(hWaitable, 0) == WAIT_OBJECT_0;
}
```
If not ready, the render thread yields or sleeps (`os_sleepto_ns()`), causing CPU context switches.

#### C++20 vs C++23 Coroutine Capabilities
- **C++20 Baseline:** Complete language keywords (`co_await`, `co_yield`, `co_return`) and low-level `<coroutine>` header. Custom awaiters (`GpuWaitableAwaiter`, `TimelineFenceAwaiter`) can register handles with the Windows Threadpool (`RegisterWaitForSingleObject`):
  ```cpp
  struct GpuFenceAwaiter {
      HANDLE waitableObject;
      bool await_ready() const noexcept {
          return WaitForSingleObject(waitableObject, 0) == WAIT_OBJECT_0;
      }
      void await_suspend(std::coroutine_handle<> handle) noexcept {
          RegisterWaitForSingleObject(&waitHandle, waitableObject, WaitCallback, handle.address(), INFINITE, WT_EXECUTEONLYONCE);
      }
      void await_resume() const noexcept {}
  };
  ```
- **C++23 Enhancements:** Standard library introduces `std::generator<T>` (`<generator>`, P2502R2). A presentation compositor can yield active video layers on demand without allocating intermediate layer vectors:
  ```cpp
  std::generator<const VideoFrame*> getActiveLayers() const {
      for (uint32_t i = 0; i < kMaxLayers; ++i) {
          if (!pool_free_[i]) {
              co_yield reinterpret_cast<const VideoFrame*>(pool_bytes_ + (i * sizeof(VideoFrame)));
          }
      }
  }
  ```

### 2.4 Multi-Track Compositing with C++23 Ranges
C++23 expands `<ranges>` with essential adaptors that streamline scene composition:
1. `std::views::zip`: Pairs active compositor video tracks with their respective PTS timestamps and alpha blend states in a single pass without index indexing:
   ```cpp
   for (auto&& [layer, transform, blend] : std::views::zip(activeLayers, layerTransforms, blendModes)) {
       renderLayer(layer, transform, blend);
   }
   ```
2. `std::views::chunk`: Batches multi-view camera sources for grid layout rendering:
   ```cpp
   for (auto cameraRow : cameraSources | std::views::chunk(2)) {
       renderGridRow(cameraRow);
   }
   ```
3. `std::ranges::to`: Concurrently materializes transformed pipeline outputs directly into standard containers:
   ```cpp
   auto activeIds = sceneList 
                  | std::views::filter([](auto& s) { return s.visible; })
                  | std::views::transform([](auto& s) { return s.id; })
                  | std::ranges::to<std::vector>();
   ```

---

## 3. Qt6 Desktop UI Integration Analysis

### 3.1 Platform Baseline & MOC Interoperability
LibreCast Studio integrates prebuilt Qt 6.11.1 (`.deps/obs-deps-qt6-2026-07-15-x64`). Qt's Meta-Object Compiler (MOC) generates reflection metadata for `Q_OBJECT` classes (`OBSApp`, `OBSMainWindow`).

#### MOC Compatibility Findings
- **C++20:** Fully supported in Qt 6.11.1. MOC parses C++20 concepts, spaceship operator (`<=>`), and standard headers.
- **C++23:** Qt 6.11.1 compiles cleanly under C++23 (`/std:c++latest`). However, MOC cannot parse C++20 modules (`export module`) or Deducing `this` within `Q_OBJECT` macro declarations. Modern C++23 constructs must be placed in normal member functions outside `signals:` and `slots:` blocks.

### 3.2 Macro Hygiene & Keyword Collisions (`QT_NO_KEYWORDS`)
Qt headers by default declare macros for `signals`, `slots`, and `emit`:
```c
#define slots
#define signals protected
#define emit
```
In real-time graphics and hardware encoders, identifiers named `emit` or `slots` frequently collide (e.g., event emitter classes, descriptor slots in DirectX 12).
- **Mandatory Hygiene Rule:** Define `QT_NO_KEYWORDS` globally in CMake (`add_compile_definitions(QT_NO_KEYWORDS)`).
- **Enforcement:** Code must exclusively use `Q_SIGNALS`, `Q_SLOTS`, and `Q_EMIT`.
- **Empirical Validation:** Verified in `spikes/cpp20_vs_cpp23/qt6_cpp23_spike.cpp` and `macro_collisions_spike.cpp`.

### 3.3 UTF-8 String Literal Evolution (`char8_t`)
In C++20 and C++23, string literals prefixed with `u8` have type `const char8_t[N]` (P0482R6), breaking implicit conversion to `const char*` or `std::string`.

#### Qt 6.11.1 Native `char8_t` Support
Direct inspection of `include/QtCore/qstring.h` (lines 787–791) confirms Qt 6.11.1 includes native `char8_t` overloads:
```cpp
static inline QString fromUtf8(const char8_t *str)
{ return fromUtf8(reinterpret_cast<const char *>(str)); }

static inline QString fromUtf8(const char8_t *str, qsizetype size)
{ return fromUtf8(reinterpret_cast<const char *>(str), size); }
```
`QString::fromUtf8(u8"Stream Live")` compiles seamlessly under C++23 without errors.

#### Legacy C String Compatibility
Because `libobs` core and third-party plugins contain legacy C APIs expecting `const char*` for UTF-8 string literals, the build system must retain:
- MSVC: `/Zc:char8_t-`
- Clang: `-fno-char8_t`
This allows legacy plugins to continue compiling while modernized Qt6 UI components leverage type-safe UTF-8 conversions.

### 3.4 String Formatting: `QString::arg` vs `std::format` vs `std::print`
- **Legacy `QString::arg`:** Incurs successive string allocations and intermediate parsing for every `%1`, `%2` placeholder:
  ```cpp
  QString s = QString("Frame %1 dropped at %2 ms").arg(count).arg(pts);
  ```
- **C++20 `std::format`:** Type-safe, compile-time format string validation (P0645R10), eliminating printf security vulnerabilities:
  ```cpp
  std::string s = std::format("Frame {} dropped at {} ms", count, pts);
  ```
- **C++23 `std::println`:** Prints directly to `stderr` or Unicode console handles without allocating an intermediate `std::string` or locking `<iostream>` buffers:
  ```cpp
  std::println(stderr, "Critical pipeline error: {:#x}", errorCode);
  ```

---

## 4. Native DirectX 11/12 Graphics Pipelines

### 4.1 Subsystem Status: `libobs-d3d11` vs Greenfield DirectX 12
The active Windows graphics renderer is `libobs-d3d11/`, which utilizes D3D11 immediate context, FLIP_DISCARD swap chains, and legacy HLSL Shader Model 4.0 shaders compiled via `d3dcompiler_47.dll`. D3D12 currently exists only in external hook capture (`plugins/win-capture/graphics-hook/d3d12-capture.cpp`) using `d3d11on12`.

### 4.2 DirectX 12 Agility SDK Architecture
Building a native `libobs-d3d12` renderer requires modern D3D12 features decoupled from specific Windows OS update branches.
- **Agility SDK Distribution:** The executable exports two symbols defining the runtime loader configuration:
  ```cpp
  extern "C" {
      __declspec(dllexport) extern const UINT D3D12SDKVersion = 614; // Agility SDK 1.614+
      __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\";
  }
  ```
- **Key Agility Capabilities Enabled:**
  1. **Enhanced Barriers:** Replaces coarse resource barrier structs with fine-grained subresource state transitions (`D3D12_BARRIER_SUBRESOURCE_RANGE`).
  2. **Shader Model 6.6+:** Dynamic resource binding and 64-bit integer atomics compiled via DXC (`dxcompiler.dll`).
  3. **DirectStorage Integration:** High-throughput streaming of video assets directly from NVMe storage to VRAM.

### 4.3 Vector & Matrix Math Modernization: DirectXMath SIMD Alignment
The OBS graphics subsystem currently computes 4x4 matrix multiplication via nested scalar loops in `libobs/graphics/matrix4.c` (lines 60–76), performing 16 iterations using scalar dot products.

#### DirectXMath (v320) Integration
`DirectXMath.h` is distributed with Windows SDK 10.0.26100.0/10.0.28000.0 and is cross-platform (open-sourced with ARM NEON support).
- **SIMD Performance:** `XMMatrixMultiply` computes $4\times 4$ transforms using 4 shuffle instructions and 4 fused multiply-adds, yielding a **~4x throughput improvement** over scalar loops.

#### Memory Alignment Rule: Unaligned Storage vs 16-Byte SIMD Registers
- `DirectX::XMVECTOR` and `DirectX::XMMATRIX` require strict 16-byte alignment (`__m128`). Dynamic allocations via OBS's C allocators (`bmalloc`) do not guarantee 16-byte alignment and will cause hardware general protection faults (`#GP`).
- **Architectural Solution:**
  - Structs and persistent heap memory MUST use unaligned storage types (`DirectX::XMFLOAT4X4`, `DirectX::XMFLOAT4`).
  - SIMD operations load into registers on the stack (`XMLoadFloat4x4`), execute vector math, and store results back (`XMStoreFloat4x4`).

```cpp
struct SceneTransform {
    DirectX::XMFLOAT4X4 worldMatrix; // Unaligned storage (safe in heap structs)
};

void updateTransform(SceneTransform& transform) {
    DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(&transform.worldMatrix);
    DirectX::XMMATRIX rot = DirectX::XMMatrixRotationZ(0.016f);
    DirectX::XMStoreFloat4x4(&transform.worldMatrix, DirectX::XMMatrixMultiply(m, rot));
}
```

### 4.4 Zero-Copy `TextureHandle` Cross-API Interface
`zero-copy-texture-interface-spec.md` specifies the unified handle passing mechanism across graphics backends (D3D11, D3D12, Vulkan) and hardware encoders (NVENC, AMF, QSV):
```cpp
enum class GraphicsAPI : uint32_t {
    Vulkan = 0,
    DirectX12 = 1,
    DirectX11 = 2,
    Metal = 3,
    OpenGL = 4
};

struct TextureHandle {
    GraphicsAPI api;
    uint64_t native_handle; // ID3D12Resource*, VkImage, or ID3D11Texture2D*
    int32_t shared_fd;       // NT shared handle or Linux DMA-BUF fd
    uint32_t width;
    uint32_t height;
    uint32_t frame_seq;
    uint64_t pts;
    uint32_t _padding;
};
```
By mapping `TextureHandle.native_handle` directly to a C++23 `std::mdspan`, presentation layers can inspect subresource dimensions and strides while maintaining zero-copy GPU memory sharing.

---

## 5. Toolchain & Compiler Support Matrix

### 5.1 Host Compiler Support Matrix
| Compiler | Minimum Required Version | Recommended Version | C++20 Flag | C++23 Flag | Status for LibreCast Studio |
|---|---|---|---|---|---|
| **MSVC** | 19.44 (VS 2022 17.14) | 19.51 (VS 2026 / 18.11) | `/std:c++20` | `/std:c++latest` | **Verified.** Full support for `<expected>`, `<mdspan>`, `<print>`, `<generator>`. (Note: MSVC gates C++23 behind `/std:c++latest`). |
| **LLVM Clang** | Clang 17.0 | Clang 22.1.8 | `-std=c++20` / `/std:c++20` | `-std=c++23` / `/std:c++latest` | **Verified.** Auto-discovers MSVC STL on Windows. 100% C++23 language & header support. |
| **GCC (Linux)** | GCC 13.1 | GCC 14.2+ | `-std=c++20` | `-std=c++23` | **Supported.** libstdc++ implements full C++23 core and stdlib. |
| **Apple Clang** | Xcode 15.3 (Clang 15.0) | Xcode 16.0+ (Clang 16.0) | `-std=c++20` | `-std=c++23` | **Supported.** libc++ supports `<expected>`, `<format>`, and `<span>`. `<mdspan>` requires LLVM 18+. |

### 5.2 Build System & CMake Baseline
- **Minimum CMake Version:** `3.28` (mandated by existing `CMakePresets.json`).
- **Recommended CMake Version:** `4.4.3` (verified on host environment).
- **Target Compile Features:**
  - Modernized targets specify `target_compile_features(<target> PUBLIC cxx_std_23)`.
  - CMake translates `cxx_std_23` automatically to `/std:c++latest` on MSVC and `-std=c++23` on Clang/GCC.

---

## 6. Compilation Spikes & Empirical Diagnostic Findings

A comprehensive suite of twelve compilation spikes was developed and executed on the host using Clang 22.1.8 targeting `x86_64-pc-windows-msvc` against Qt 6.11.1 and Windows SDK 10.0.28000.0.

### 6.1 The 20 Verbatim Compiler Errors Under C++20
When compiling C++23 deep features (`spikes/cpp20_vs_cpp23/cpp23_deep_features.cpp`) under `/std:c++20`, the MSVC STL headers gate modern types behind `_HAS_CXX23 = 0`, producing exactly **20 verbatim compiler errors**:

```text
1.  error: no template named 'expected' in namespace 'std'
2.  error: no member named 'unexpected' in namespace 'std'; did you mean simply 'unexpected'?
3.  error: too many arguments to function call, expected 0, have 1
4.  error: no template named 'expected' in namespace 'std'
5.  error: no member named 'unexpected' in namespace 'std'; did you mean simply 'unexpected'?
6.  error: too many arguments to function call, expected 0, have 1
7.  error: no member named 'mdspan' in namespace 'std'; did you mean 'span'?
8.  error: use of undeclared identifier 'frame'
9.  error: no member named 'println' in namespace 'std'
10. error: no template named 'generator' in namespace 'std'; did you mean 'iterator'?
11. error: too few template arguments for class template 'iterator'
12. error: std::coroutine_traits type was not found; include <coroutine> before defining a coroutine
13. error: no member named 'stacktrace' in namespace 'std'
14. error: no member named 'zip' in namespace 'std::ranges::views'
15. error: no member named 'chunk' in namespace 'std::ranges::views'
16. error: no member named 'to' in namespace 'std::ranges'
17. error: expected '(' for function-style cast or type construction
18. error: expected expression
19. error: explicit object parameters are incompatible with C++ standards before C++2b
20. error: no member named 'to_underlying' in namespace 'std'
```
*Conclusion:* C++23 features are strictly unavailable under `/std:c++20`. Utilizing them without upgrading the standard would require introducing fragmented third-party polyfills (`tl::expected`, Kokkos `mdspan`).

### 6.2 Windows SDK & Macro Collisions Spike
Legacy Windows SDK headers pollute the global preprocessor namespace. Four critical collisions were reproduced and resolved in `spikes/cpp20_vs_cpp23/macro_collisions_spike.cpp`:

1. **`min` / `max` Collision (`minwindef.h:193`):**
   - *Diagnostic:* Corrupts `std::max(a, b)` and `std::numeric_limits<int>::max()` with `error: expected unqualified-id`.
   - *Resolution:* Define `NOMINMAX` globally before including `<windows.h>`.
2. **`small` Collision (`rpcndr.h:201`):**
   - *Diagnostic:* Macro `#define small char` breaks struct member `int small;` with `error: cannot combine with previous 'int' declaration specifier`.
   - *Resolution:* Enforce `#undef small` after RPC headers.
3. **`near` / `far` Collision (`minwindef.h:28`):**
   - *Diagnostic:* Macros `#define near` and `#define far` break camera frustum clipping fields `float near, far;` with `error: declaration does not declare anything`.
   - *Resolution:* Enforce `#undef near` and `#undef far` after Windows headers.
4. **`interface` Collision (`combaseapi.h:168`):**
   - *Diagnostic:* Macro `#define interface __STRUCT__` breaks enum or method `NetworkType::interface` with `error: expected identifier`.
   - *Discovery:* DirectX headers (`d3d11.h`, `d3d12.h`) require the `interface` macro.
   - *Resolution:* Enforce `#undef interface` strictly **after** all DirectX headers have been included.

### 6.3 Empirical Build Time & Parse Overhead Benchmark
An apples-to-apples translation unit parse benchmark was executed comparing `unified_pipeline_c20.cpp` (Windows SDK + DirectX 11/12 + DirectXMath + Qt6 + C++20 `<format>`, `<span>`, `<ranges>`) against `unified_pipeline_spike.cpp` (Windows SDK + DirectX 11/12 + DirectXMath + Qt6 + C++23 `<expected>`, `<mdspan>`, `<print>`, `<ranges>`):

| Pipeline Spike Variant | Standard Flag | Target Headers Included | Measured Parse Time |
|---|---|---|---|
| **C++20 Unified Pipeline** | `/std:c++20` | Windows SDK, D3D11/12, DirectXMath, Qt6 Core, `<span>`, `<ranges>`, `<format>` | **2,277 ms** |
| **C++23 Unified Pipeline** | `/std:c++latest` | Windows SDK, D3D11/12, DirectXMath, Qt6 Core, `<expected>`, `<mdspan>`, `<print>`, `<ranges>` | **2,533 ms** |
| **Measured Parse Overhead** | — | — | **+11.2% (256 ms)** |

*Conclusion:* The 11.2% parse overhead is negligible for modern developer workstations and CI runners, especially when weighted against the safety and zero-allocation performance benefits of `std::mdspan` and `std::expected`.

---

## 7. Hybrid C++ / Rust FFI Boundary Strategy

### 7.1 Current Codebase State
The codebase survey verified that LibreCast Studio currently contains 0 Rust source files (`.rs`), 0 `Cargo.toml`, and 0 `bindgen`/`cxx` bridge files. However, upcoming modernization milestones introduce Rust crates for transport protocols (WHIP/WHEP, SRT) and media security.

### 7.2 Toolchain Interoperability Constraints
1. **Rust `bindgen` (Pure C ABI):**
   - `bindgen` relies on `libclang` to parse C/C++ headers.
   - While `bindgen` parses standard C declarations effortlessly, it cannot generate usable Rust bindings for complex C++23 template instantiations (`std::expected<T, E>`, `std::mdspan<T, Extents>`, `std::generator<T>`), generating opaque byte buffers (`[u8; N]`).
2. **`cxx` Bridge Limitations:**
   - `cxx` generates bidirectional bindings for a curated subset of C++ types (`std::string`, `std::vector`, `std::unique_ptr`, `rust::Slice<T>`).
   - `cxx` does not support C++23 vocabulary types (`std::expected`, `std::mdspan`). Rust's `Result<T, E>` maps only to C++ exceptions via `cxx`.

### 7.3 Canonical Architectural Boundary Rule
To ensure robust interoperability across hybrid language boundaries:
> **Strict FFI Encapsulation Rule:**  
> All public API headers consumed by Rust `bindgen` or exposed across foreign function boundaries MUST remain pure C ABI (`extern "C"`, opaque pointers, POD structs).  
> All C++23 template types (`std::expected`, `std::mdspan`, `std::generator`, `std::span`) MUST be encapsulated strictly within C++ implementation files (`.cpp`).

```
[ Rust Transport / Protocol Crates ]
               ▲
               │ Pure C ABI (extern "C", POD structs, TextureHandle) via bindgen
               ▼
[ libobs Public C FFI Boundary ]
               ▲
               │ Internal C++ Adapter
               ▼
[ Modern C++23 Engine Core: SceneCompositor, D3D11/12, std::mdspan, std::expected ]
```

---

## 8. Summary Comparison Matrix: C++20 vs C++23 Feature Tradeoffs

| Capability | C++20 Baseline | C++23 Modernization Baseline | Recommendation for LibreCast Studio |
|---|---|---|---|
| **Strided Frame Layouts** | Manual 1D index calculation (`ptr + y * pitch + x`) | Native `std::mdspan` with `std::layout_stride` and `operator[y, x]` | **Adopt C++23.** Eliminates buffer calculation bugs in video compositor. |
| **Error Propagation** | Throwing `HRError` exceptions or error code booleans | `std::expected<T, E>` with monadic chaining | **Adopt C++23.** Zero-allocation, exception-free error paths in render loops. |
| **Diagnostics & Logging** | `std::format` (allocates temporary `std::string`) | `std::println` (direct unbuffered Unicode console output) | **Adopt C++23.** Eliminates heap allocation during telemetry output. |
| **Scene Layer Composition** | Manual index-based for loops | `views::zip`, `views::chunk`, `ranges::to` | **Adopt C++23.** Expressive, bug-free multi-layer compositor loops. |
| **Swapchain Synchronization** | Low-level `<coroutine>` awaiters | `<coroutine>` + `std::generator` for frame iteration | **Adopt C++23.** Allows coroutine generation of active rendering layers. |
| **Qt6 UI Integration** | Full Qt 6.11.1 support | Full Qt 6.11.1 support + native `char8_t` `QString::fromUtf8` | **Adopt C++23.** Type-safe UTF-8 string handling. |
| **MSVC Compiler Maturity** | Stable `/std:c++20` (`_MSVC_LANG = 202002L`) | Stable `/std:c++latest` (`_MSVC_LANG = 202400L`) | **Adopt C++23.** Both MSVC 19.44 and 19.51 provide complete C++23 stdlib. |
| **Clang Compiler Maturity** | Clang 16+ (`-std=c++20`) | Clang 22.1.8+ (`-std=c++23`) | **Adopt C++23.** 100% C++23 support verified on host. |
| **Rust FFI Interop** | Encapsulated C ABI required | Encapsulated C ABI required | **Neutral.** Strict C ABI boundary required under both standards. |

---

## 9. Verification & Audit Trail
All empirical claims and compiler diagnostics documented in this report are independently reproducible via the compilation spikes located in `spikes/cpp20_vs_cpp23/`:
- Test runner: `spikes/cpp20_vs_cpp23/run_spikes.ps1`
- Detailed diagnostic log: `spikes/cpp20_vs_cpp23/SPIKE_RESULTS.md`
- Verification execution: 12 / 12 tests verified passing on 2026-10-29.
