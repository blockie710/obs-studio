# LibreCast Studio — Slint Pilot: Visual/UX Spec

Source of record: Google AI Studio demo build at `C:\Users\bhogl\Downloads\librecast-studio`
(React 19 + Tailwind mockup — **UI/UX reference only**, no engine code is portable).
Purpose: a screen-by-screen contract for what the Slint pilot should look and behave like,
plus which pieces map to surfaces that already exist in the Qt6 tree.

> **Honest framing up front.** This demo is a web mockup. The spec below is the *visual &
> interaction contract* to reproduce in Slint. Nothing here implies performance numbers —
> none of the telemetry values (VMAF 97.4, jitter 142µs, 18.2 GB/s zero-copy) are measurements.
> When porting, treat them as dead labels to be wired to real Rust/C++23 data, not as targets.

---

## 1. Design language (global)

Derived from Tailwind classes in the demo — this is the Slint theme target.

| Token | Value (demo) | Notes |
|---|---|---|
| App background | `#0a0a0a` (neutral-950) | window + canvases |
| Dock background | `#171717` (neutral-900) | scenes/sources, mixer, stats |
| Panel/well inset | `#0a0a0a`/`neutral-950` | cards, sliders, inputs |
| Border | `#262626`–`#404040` (neutral-800/700) | 1px, radius 8px (rounded-lg) |
| Primary accent | `#22d3ee` (cyan-400) | links, active nav, "Go Live" idle, sliders |
| Live / danger | `#e11d48`/`#ef4444` (rose-500/600) | streaming active, clip, record |
| Success | `#34d399` (emerald-400) | meters safe zone, VMAF, gate open |
| Warning | `#fbbf24` (amber-400) | meter target zone, boost, comp active |
| Mono text | `#94a3b8` (slate-400) | telemetry, small labels — JetBrains Mono |
| Body text | `#e5e5e5`–`#f5f5f5` (neutral-100/200) | Plus Jakarta Sans (demo uses it) |

**Recurring motifs (spec these once, reuse everywhere):**
- **Segmented control** — pill container + filled active segment (mode, OS, meter layout).
- **Status LED pill** — dot + mono label in a bordered pill w/ glow (`GATE OPEN`, `LIMITING`, `LIVE`).
- **Hardware-meter look** — segmented LED bars (diode stack) + 1s peak-hold needle that fades.
- **Quick-preset cards** — 2×2 grid, emoji title + mono parameter readout.
- **Clean mono telemetry strip** — values separated by `·` / `|`, no boxes.
- **Bottom status bar** — mono, left subsystem summary, right metrics + license.

---

## 2. Top bar (HeaderBar)

Three stacked rows (Slint: this is a `VerticalLayout` of 3 bands).

**Row A — native title bar.** Left: brand glyph + "LibreCast Studio (Desktop Application · Slint Native GUI)" + mono `GPLv3+ · C++23 & Rust Core`. Right: min/max/close window controls (Slint + OS chrome).

**Row B — menu strip.** `File · Edit · View · Profile · Scene Collection · Tools · Help`. Budget items from `MENUS` array (HeaderBar.tsx:36-44). Note `Tools` already lists *Slint UI Inspector* and *Kalman Filter Calibration* — good pilot hooks.

**Row C — primary action bar** (3 zones):
- Zone 1: brand wordmark "LibreCast" + mono `Slint GUI · <OS> Native`.
- Zone 2: nav tabs — **Director Studio · Audio DSP Racks · Statistical Telemetry · Scripting & Slint UI**.
- Zone 3: `Download Desktop App` (ghost) · `Record` (mute icon) · **`Go Live`** (cyan→rose when live, key CTA) · `Settings` (icon).

Slint: the CTA and Record toggle are the pilot's proof-of-state-changes candidates.

## 3. Mode & OS bar (ModeAndOsBar)

- **Mode segmented control:** `Streamlined (Amateur)` / `Studio Pro (Broadcaster)`.
- **Target OS segmented control:** `Windows (ASIO / D3D12)` · `Linux (PipeWire / Vulkan)` · `macOS (CoreAudio / Metal)` — OS switch re-derives audio driver + encoder (App.tsx:65-79).
- **Right telemetry strip** (mono, separated): `60.0 FPS · 16.67ms · σ=142µs | VMAF 97.4 · 8.00 Mbps | Zero-Copy: DXGI/DMA-BUF/IOSurface · ASIO 64s`. OS-dependent strings.

Pilot decision: drive these labels from real state, not hardcoded strings.

## 4. Director Studio canvas (BroadcastCanvas)

**Studio Pro mode** — three columns:
- **Preview monitor** (left, emerald header `PREVIEW MONITOR`): live scene.
- **Transition director** (center rail): `CUT` / `AUTO (300ms)` buttons + **T-Bar slider** (0–100%) + duration select (150/300/500/1000 ms) + `WebCam` toggle.
- **Program monitor** (right, rose header `PROGRAM MONITOR (ON AIR)`, pulsing `LIVE` badge when streaming).

**Streamlined mode** — single centered monitor + header (`1080p60 Hardware Accelerated`, `STREAMING LIVE`) + `Start Live Camera` + bottom **Quick Scenes** chip strip.

**On-canvas source overlays** (drawn, not real): game capture shows animated particle/grid with mono `DIRECTX 12 / DMA-BUF ZERO-COPY CAPTURE (120 FPS)` label; camera shows simulated frame + mic wavebar; `telemetry_hud` type draws a live stats box; `browser` draws lower-thirds. **These are the rendering surfaces that Slint must present (window into real composited frame) — in the pilot a placeholder rect is fine.**

Interaction: click source → amber dashed selection box on canvas.

## 5. Scenes & Sources dock (ScenesAndSourcesDock) — two columns

- **Scenes** (left, 192px): list w/ `PGM` (rose) / `PVW` (emerald) badges; single-click → preview, double-click → cut live; `+` to add.
- **Sources** (right): rows w/ type icon (camera/game/screen), eye + lock toggles; **quick-add buttons** `Camera · Game · Screen`; footer mono `Transform: 1920x1080 (Zero-Copy Texture Share) · GPU Accelerated`.

This dock **maps to the existing Qt6 Scenes/Sources in OBS** — it's a restyle, not net-new.

## 6. Audio Mixer dock (AudioMixerDock)

Dock header controls: `Audio Mixer` + driver badge (`ASIO Low-Latency`), **LED color legend**, **layout toggle** (horizontal meters / vertical faders), `Scan Plugins`, `Test Mic`, `Test Tone (440Hz)`, **Advanced Audio Properties** (gear → matrix modal).

Per-channel (horizontal): **numeric dB gain readout** (click to type a value, ↑/↓ steps 0.5/1.0 dB, `BST` badge when >+1.6dB boost), **segmented LED meter** (green −60…−20, amber −20…−9, red −9…0; 1s peak-hold needle that fades; clip flashes 1.2s), mute / solo / lock controls. Vertical mode = stacked fader columns.

**DSP modals** (each a reusable pattern):
- **Noise Gate** — power switch, live `GATE OPEN/CLOSE` pill (+GR dB), signal-vs-threshold bar, sliders for Threshold/Attack/Release/Hold + **4 quick DSP presets**, Reset Defaults.
- **Limiter** — same skeleton; Brickwall ceiling bar, `LIMITING ACTIVE / HEADROOM OK`, Threshold + Release + 4 presets.
- **Compressor** — Threshold/Ratio/Attack/Release/Output-gain + a **Gain Reduction meter** + 4 presets.
- **Advanced Audio Properties** (SettingsModal) — **6-track routing matrix** (Master/Mic/Game/Discord… × output tracks), pan, sync offset, mono, monitoring.

Mixer maps to Qt6's existing Audio Mixer + Advanced Audio Properties — restyle + DSP modal patterns.

## 7. Statistical Engine dock (StatisticalEngineDock)

3-card grid: **Frame-Time Jitter (σ)** w/ sparkline histogram (~10 bars); **Perceptual Quality (VMAF)** w/ SSIM readout + gauge bar; **Kalman Drop Forecast** w/ bitrate readout + slider (2500–15000 kbps) + dropped-frames count. Bottom bar: encoder, latency, VRAM, `GPLv3+`.

> These are the flagship *new* surfaces — no direct Qt6 equivalent today. **This is the natural Slint pilot target**: dense, live-bound readouts + sparklines, exactly where declarative bindings shine. In the pilot, wire to a Rust metrics struct (or stub values) rather than `Math.random()`.

## 8. Scripting & Slint UI duck (ScriptingStudioDock)

Script list where each entry has a language tag: `slint_ui` · `rust_wasm` · `cpp23_module` · `lua`, status `running/idle/error`, `lastRunTimeMs`, editable code pane + run toggle.

The demo ships example scripts that are *aspirational skeletons*:
- `BroadcastControlSurface.slint` — an actual Slint component sketch (Slint DSL, `BackingStore`-style `inherits Window`, Slider bound to `t_bar_position`, `default-font-family`). **This is a ready-made first file for the pilot.**
- `KalmanBitrateGovernor` (Rust wasm) + `composite_nv12_fast` (C++23 SIMD) + Lua auto-director — all stubs.

## 9. Plugin Rack modal (PluginRackModal)

Per-channel DSP chain rack: list of loaded plugin instances (name, vendor, format badge `VST3/CLAP/LV2/AU`, category, latency), enable bypass, param sliders (id/name/value/min/max/step/unit), gain-reduction readout, add-from-catalog, **open scanner**.

## 10. Plugin Scanner modal (PluginScannerModal + pluginScanner.ts)

- OS-aware default directories (Windows VST3/CLAP, Linux VST3/CLAP/LV2, macOS AU/VST3/CLAP — see `DEFAULT_DIRECTORIES`).
- Scan simulation: progress line (dir + `Scanning <fmt> bundles in <path>…`, 220ms steps).
- Results list: name, vendor, version, format, `validationStatus` (verified/sandboxed/quarantined), arch; **Host** a plugin into a channel rack; load an arbitrary local file.

## 11. Settings modal (SettingsModal)

Tabs: **Multi-Track Recording · Hotkeys · Profiles · Video · Output · Audio · About**.
- **Multi-Track**: 6 recording tracks (name, codec AAC/Opus/PCM_24bit, bitrate, mono/stereo), container (mkv/mov/mp4/ts), auto-remux, per-channel→track route matrix.
- **Hotkeys**: rebindable shortcuts w/ capture, categories (Broadcast & Output / Scene Switching / Audio & Muting / Studio Transitions). Defaults in `DEFAULT_HOTKEYS` (Ctrl+Shift+S stream, Alt+1..4 scenes, Ctrl+M mute mic, F5/F6 transitions). **Qt6 already has a hotkey system — restyle + keep backing store.**
- **Profiles**: 4 factory `StreamingOutputProfile`s (Twitch 1080p60 CBR, YouTube 4K HDR AV1, TikTok 9:16, Esports 1440p120), JSON export/clone/delete, "factory defaults" reset.
- **Video / Output / Audio**: standard encoder/bitrate/rate-control + color-space/range sections.
- **About**: GPLv3+.

## 12. Native packaging modal (NativePackagingModal)

Per-OS download hub: **Windows** (`.exe` installer), **Linux** (`.AppImage` + `.deb`), **macOS** (`.dmg`). Shows artifact filenames + simulated download progress, Promise → `Preparing…`.

---

## A. Portability map (demo → existing Qt6 tree)

| Demo surface | Qt6 analog today | Verdict |
|---|---|---|
| Scenes & Sources dock | OBS dock | **Restyle** (retheme, keep data) |
| Audio Mixer + Advanced props | OBS audio mixer/APA | **Restyle**, add DSP-modal pattern |
| Hotkeys / Settings tabs | OBS settings + hotkey dialog | **Restyle**, keep backing store |
| Native packaging | installer scripts (Win/Linux/mac) | Net-new front-end shell |
| Plugin rack / scanner | plugin infra (obs-ffmpeg, ASIO) | Net-new UI; **data already real** |
| Statistical telemetry dock | None | **Net-new — best Slint pilot target** |
| Scripting & Slint UI duck | OBS Scripts (Lua) | Extend; add Rust/C++/Slint script types |

## B. Recommended pilot slice (bounded, high-evidence)

1. **Statistical Engine dock** — demonstrates Slint's reactive-binding strength on live data you actually have.
2. **One DSP modal** (Compressor) — proves the slider/numeric/status-pill pattern + preset cards.
3. **`BroadcastControlSurface.slint`** (from demo scripts) compiled as a real component — proves the Slint DSL ↔ C++/Rust callbacks round-trip (T-Bar → `transition_triggered`, buffer-size → `changed`).
4. **Header CTA state** (Go Live idle→live) — cheapest proof of declarative state styling.

Do these four behind a Slint flag/entry point. Keep Qt6 as the app host; Slint renders the pilot window/surface. Judge on: integration cost, DSL ergonomics vs QML, toolchain/event-loop bridging, and whether the *net-new* surfaces (telemetry) are worth the added dependency. If painful, you've spent days not a rewrite.

## C. Anti-port (do NOT carry over)

- All `Math.random()` telemetry / jitter / VMAF / drop-risk values — real data only.
- Fabricated performance claims in labels ("AVX-512", "0.12ms", "Bayesian Kalman", "v0.9.4") — either genuinely implement or drop the labels.
- The AI-Studio branding ("AetherOBS", "aether_stats") vs LibreCast naming.
- Simulated plugin names referencing real commercial products ("FabFilter Pro-L 2 Clone", "Universal Audio 1176") — legal/3P branding exposure.

## D. Build & verify the pilot (scaffold as of branch `experimental/slint-pilot`)

The pilot is the Slint + C++ stub under `experimental/slint/` — `BroadcastControlSurface.slint`,
`pilot_main.cpp`, `CMakeLists.txt` (Slint v1.8.0 via FetchContent). It is **excluded from the
default build**; stable/`master` builds are unaffected.

Prereqs for ANY configure on this repo (pre-existing, independent of the pilot):
the git submodules `plugins/obs-browser` and `plugins/obs-websocket` must be initialized —
`git submodule update --init` (a bare configure fails at `plugins/CMakeLists.txt` otherwise).

```bash
# 1) Configure WITHOUT the pilot (the stable path) — must stay green:
cmake -S . -B build -G "Visual Studio 17 2022" -DENABLE_SLINT_PILOT=OFF

# 2) Configure WITH the pilot (first run pulls Slint from GitHub, needs network):
cmake -S . -B build-pilot -G "Visual Studio 17 2022" -DENABLE_SLINT_PILOT=ON

# 3) Build just the pilot:
RUSTFLAGS="-A dangerous_implicit_autorefs" \
  cmake --build build-pilot --config Release --target librecast-slint-pilot
```

**Why `RUSTFLAGS` is required (verified 2026-11-05):** Slint 1.8.0's Rust crates don't
compile on rustc 1.98 (newer than Slint's toolchain baseline). Modern rustc turns
`dangerous_implicit_autorefs` into a deny-by-default lint; `slint-interpreter` trips it at
`dynamic_type.rs:170`. The `-A` allows that one lint and unblocks the build — an upstream
lockstep issue to factor into the framework verdict (§B). Long-term: pin a newer Slint or an
older Rust stable.

**Header-name convention (recent porting gotcha):** Slint's CMake generates the C++ header from
the `.slint` file's base name in PascalCase — `BroadcastControlSurface.slint` → include
`"BroadcastControlSurface.h"`, NOT a lowercase slug. (Slint's own Rust/JS APIs use a different
naming; the C++ generator uses `NAME_WE` of the file.)

Success = `(2)` configures and `(3)` produces `librecast-slint-pilot.exe`; the app shows the
T-Bar surface and prints the callback round-trips to stderr. The flag being default-OFF is
the contamination guard: `git status` on `master` must show no staged pilot files.

## E. Verified state (2026-11-05)

- Pilot-OFF configure: **passes** (exit 0; only obs-browser/obs-libfdk/test-input disabled).
- Pilot-ON configure: **passes** (exit 0; Slint v1.8.0 fetched via FetchContent, target `librecast-slint-pilot` registers in the VS build graph).
- **Pilot builds and RUNS.** `cmake --build build-pilot --config Release --target librecast-slint-pilot` exits 0
  and produces `experimental/slint/Release/librecast-slint-pilot.exe` (92 KB). Launching it opens a
  window titled **"LibreCast Slint Pilot"** that renders the surface — pixel-verified: bg `#0a0a0a`,
  cyan `#22d3ee` title text, slider + status + button present. It stays alive and responsive
  (WM_GETTITLE, IsWindowVisible, PrintWindow all succeed; process runs until killed).
- **Toolchain friction (reproducible, upstream):** building Slint 1.8.0 on Rust 1.98.0 fails —
  modern rustc's deny-by-default `dangerous_implicit_autorefs` lint rejects
  `slint-interpreter/src/dynamic_type.rs:170`. This is a Slint-vs-newer-Rust incompatibility,
  NOT a defect in the pilot. Immediate workaround:
  `RUSTFLAGS="-A dangerous_implicit_autorefs" cmake --build ... --target librecast-slint-pilot`.
  Longer-term: pin a newer Slint release or an older Rust channel (friction to evaluate in the
  framework verdict — see §B). Workaround verified: with it, the full Slint Rust toolchain
  (`slint_cpp.dll`) compiles in ~30s.
- **Compile-time gotcha (fixed):** a full demo-dock-sized `.slint` generates one enormous
  template-heavy `slint_generated_<Base>_1.cpp` TU that takes 20–40+ min on MSVC and got
  wall-clock-killed twice before linking. The pilot surface was pared to a minimal
  Slider/Text/Button round-trip (§B items 2+4), which built to exe in minutes. Keep pilot
  `.slint` files minimal; /W0 silences MSVC's template-warning flood from slint.h.
- **CMake gotchas (fixed, both real):** `slint_target_sources` takes `.slint` files directly —
  there is NO `FILES` keyword (it becomes a phantom source and emits a failing `FILES.h` rule);
  and the generated C++ header is the file's base name in PascalCase
  (`BroadcastControlSurface.slint` → `#include "BroadcastControlSurface.h"`).
- The pared `BroadcastControlSurface.slint` keeps only two placeholder props
  (`t_bar_position`, `is_streaming`) — pure round-trip state with no fabricated values.
  Real metric surfaces (VMAF, buffer, jitter) belong in the telemetry dock later,
  wired to real Rust/C++23 data per §C — never hardcoded demo numbers.
