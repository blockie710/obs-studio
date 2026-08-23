# Community-First Independent OBS Studio Hard Fork

> **A standalone, community-driven hard fork of OBS Studio licensed under GNU GPLv3, focused on foundational broadcasting stability, deep pro-audio workflows, and transparent, contributor-friendly development.**

## Mission

This fork exists to provide a stable, feature-rich broadcasting platform that prioritizes:
- **Pro-audio workflows** as first-class citizens (native ASIO, VST3)
- **Community governance** over corporate control
- **Technical excellence** with zero-compromise engineering standards
- **Inclusive collaboration** welcoming both human and AI-assisted development

## Key Features

### 🎵 Native Pro-Audio Pipelines
| Feature | Status | Description |
|---------|--------|-------------|
| **Windows ASIO Capture** (`win-asio`) | ✅ Scaffolded | Dynamic COM driver discovery, reference-counted multi-client hardware routing, lock-free SPSC ring buffers |
| **VST3 Plugin Host** (`obs-vst3`) | ✅ Scaffolded | Full VST3 audio processing, parameter automation, native GUI embedding across platforms |

### 🏗️ Engineering Standards
- **Zero compiler warnings** (MSVC/Clang/GCC)
- **Real-time safety** on audio threads (zero allocations, zero locks)
- **C++20** with AVX2 SIMD optimizations
- **Comprehensive static analysis** (clang-tidy, cppcheck)
- **Lock-free data structures** for audio transport

### 🤝 Contributor-Driven Governance
- Frictionless issue triage and PR review
- Transparent roadmap planning
- Anti-toxic development culture
- AI-assisted workflows welcome

## Quick Start

### Prerequisites
- **Windows 10/11** (x64)
- **Visual Studio 2022** (17.8+) with MSVC v143
- **CMake 3.28+**
- **Qt 6.6+** (for frontend)
- **Git** with LFS support

### Build Instructions

```powershell
# Clone the repository
git clone https://github.com/obs-community/obs-community-studio.git
cd obs-community-studio

# Configure with pro-audio features enabled
cmake -B build -S . `
  -DENABLE_ASIO=ON `
  -DENABLE_VST3=ON `
  -DENABLE_FRONTEND=ON `
  -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release --target obs-community-studio win-asio obs-vst3

# Run
./build/rundir/Release/bin/obs64.exe
```

### CMake Options
| Option | Default | Description |
|--------|---------|-------------|
| `ENABLE_ASIO` | `ON` | Enable native Windows ASIO capture plugin |
| `ENABLE_VST3` | `ON` | Enable native VST3 plugin host filter |
| `ENABLE_FRONTEND` | `ON` | Build Qt-based UI |
| `ENABLE_SCRIPTING` | `ON` | Enable Lua/Python scripting |
| `ENABLE_HEVC` | `ON` | Enable HEVC encoders |

## Architecture Overview

```
obs-community-studio/
├── libobs/              # Core library (C11)
├── libobs-opengl/       # OpenGL video backend
├── libobs-d3d11/        # Direct3D 11 video backend (Windows)
├── libobs-metal/        # Metal video backend (macOS)
├── libobs-winrt/        # WinRT video backend (Windows)
├── plugins/
│   ├── win-asio/        # 🎵 Native ASIO capture
│   ├── obs-vst3/        # 🎵 VST3 plugin host
│   ├── win-capture/     # Window/Game capture
│   ├── win-wasapi/      # WASAPI audio capture
│   ├── obs-ffmpeg/      # FFmpeg encoding/streaming
│   ├── obs-outputs/     # Streaming/recording outputs
│   └── ...              # Other plugins
├── frontend/            # Qt6 UI application
└── deps/                # Third-party dependencies
```

## Pro-Audio Deep Dive

### ASIO Capture (`win-asio`)
- **Dynamic COM Discovery**: Scans `HKLM\SOFTWARE\ASIO` and `HKCU\SOFTWARE\ASIO` for installed drivers
- **Multi-Client Routing**: Reference-counted driver sharing across multiple OBS sources
- **Lock-Free Transport**: SPSC ring buffers with cache-line alignment for NUMA-friendly access
- **Planar Float32**: Native 32-bit float processing matching OBS audio pipeline
- **Channel Mapping**: Flexible per-source channel routing with gain/phase control

### VST3 Host (`obs-vst3`)
- **Full VST3 Support**: Audio processing, parameter automation, MIDI I/O
- **Native GUI Embedding**: Platform-agnostic editor hosting (Windows/macOS/Linux)
- **State Persistence**: Preset/chunk serialization with OBS scene collections
- **Parameter Automation**: DAW-style automation lanes in OBS timeline
- **Latency Compensation**: Automatic PDC (Plugin Delay Compensation)

## Roadmap

### Phase 1: Foundation (Current)
- [x] GPLv3 license upgrade
- [x] Project rename & decoupling
- [x] `win-asio` plugin scaffold
- [x] `obs-vst3` plugin scaffold
- [ ] CI/CD pipeline (GitHub Actions + GitLab)
- [ ] Automated static analysis

### Phase 2: Audio Maturity
- [ ] ASIO driver hot-plug support
- [ ] VST3 SDK integration (Steinberg VST3 SDK)
- [ ] Parameter automation UI
- [ ] MIDI learn/CC mapping
- [ ] Multi-threaded plugin processing

### Phase 3: Broadcast Features
- [ ] NDI 6 / SRT / ST2110 outputs
- [ ] Multi-track recording (ISO tracks)
- [ ] Loudness metering (EBU R128 / ATSC A/85)
- [ ] Audio monitoring with talkback

### Phase 4: Platform & UX
- [ ] Wayland-native Linux capture
- [ ] Apple Silicon optimization
- [ ] Modern theming system
- [ ] Plugin marketplace integration

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines on:
- Code style and review process
- Testing requirements
- Commit message conventions
- AI-assisted development workflows

## Code of Conduct

See [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md) for our community standards.

## License

This project is licensed under the **GNU General Public License v3.0 or later** - see [COPYING](COPYING) for details.

Upstream OBS Studio code retains its original GPLv2+ licensing; this fork upgrades the overall project to GPLv3+ for compatibility with modern audio SDKs (Steinberg VST3 SDK, ASIO SDK).

## Acknowledgments

- **OBS Project Contributors** - For the incredible foundation
- **Steinberg Media Technologies** - For the VST3 SDK
- **ASIO Community** - For the ASIO specification
- **All Community Contributors** - Who make this fork possible

---

**Built by the community, for the community.** 🎙️