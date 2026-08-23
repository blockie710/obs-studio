# CONTRIBUTING.md

# Contributing to the Community-First Independent OBS Studio Hard Fork

Thank you for contributing! This document outlines the guidelines and expectations for all contributors—human and AI-assisted alike.

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Development Workflow](#development-workflow)
3. [Code Standards](#code-standards)
4. [Testing Requirements](#testing-requirements)
5. [Commit & PR Conventions](#commit--pr-conventions)
6. [AI-Assisted Development](#ai-assisted-development)
7. [Review Process](#review-process)
8. [Architecture Decisions](#architecture-decisions)

---

## Getting Started

### Prerequisites

- **OS**: Windows 10/11 (x64), macOS 13+, or Linux (glibc 2.31+)
- **Compiler**: MSVC v143 (VS 2022 17.8+), Clang 17+, or GCC 13+
- **CMake**: 3.28 or later
- **Qt**: 6.6+ (for frontend)
- **Git**: 2.40+ with LFS (`git lfs install`)

### First-Time Setup

```bash
# Clone
git clone https://github.com/obs-community/obs-community-studio.git
cd obs-community-studio

# Configure (adjust paths as needed)
cmake -B build -S . \
  -DENABLE_ASIO=ON \
  -DENABLE_VST3=ON \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo

# Build
cmake --build build --config RelWithDebInfo --parallel

# Run tests
cd build && ctest --output-on-failure
```

---

## Development Workflow

### Branch Strategy

- **`main`**: Protected, always deployable. Only merges via PR.
- **`feature/*`**: New features, one per branch.
- **`fix/*`**: Bug fixes, linked to an issue.
- **`refactor/*`**: Non-functional improvements.
- **`docs/*`**: Documentation-only changes.

### Pull Request Requirements

Every PR must:

1. **Reference an issue** (or create one if none exists)
2. **Pass all CI checks** (build, static analysis, tests)
3. **Include tests** for new functionality
4. **Update documentation** if user-facing behavior changes
5. **Be rebased** on latest `main` before merge

### PR Size Guideline

- **< 400 lines changed** preferred (exceptions for generated code, vendoring)
- Split large changes into stacked PRs
- Each PR should be reviewable in < 30 minutes

---

## Code Standards

### Languages & Versions

| Component | Standard | Notes |
|-----------|----------|-------|
| libobs (core) | C11 | No C++ in core |
| Plugins | C++20 | Modules where supported |
| Frontend | C++20 / Qt6 | Modern Qt APIs only |
| Build scripts | CMake 3.28+ | No custom build systems |

### Formatting

- **C/C++**: `.clang-format` (run `clang-format -i` before commit)
- **CMake**: `cmake-format` (enforced in CI)
- **Markdown**: `prettier --prose-wrap always`
- **No tabs** — 4 spaces (C/C++), 2 spaces (CMake, Markdown)

### Naming Conventions

```c
// C (libobs)
obs_source_t*        // types: snake_case + _t
obs_source_create()  // functions: snake_case
MAX_AUDIO_CHANNELS   // constants: UPPER_SNAKE_CASE

// C++ (plugins, frontend)
class AudioProcessor  // types: PascalCase
void processAudio()   // methods: camelCase
int sample_rate_      // members: snake_case_
constexpr int kMaxChannels = 64;  // constants: kPascalCase
```

### Real-Time Audio Thread Rules

**NEVER** on the audio thread:
- ❌ Memory allocation (`new`, `malloc`, `std::vector::push_back`)
- ❌ Locks/mutexes (`std::mutex`, `pthread_mutex_lock`)
- ❌ System calls (`fopen`, `socket`, `printf`)
- ❌ Unbounded loops or recursion
- ❌ Exception throwing/catching

**ALWAYS** on the audio thread:
- ✅ Lock-free data structures (SPSC ring buffers, atomics)
- ✅ Pre-allocated buffers (pool allocators)
- ✅ Bounded, deterministic operations
- ✅ SIMD-optimized math (AVX2 intrinsics where beneficial)

### Header Hygiene

- **Self-contained**: Every header compiles standalone
- **Forward declarations** preferred over includes
- **Module maps** where supported (Clang/MSVC)
- **No `using namespace`** in headers

---

## Testing Requirements

### Test Categories

| Category | Tool | When Required |
|----------|------|---------------|
| Unit | GoogleTest | Every new function/class |
| Integration | Custom + GoogleTest | Plugin <-> core interactions |
| Audio correctness | Custom DSP tests | Any audio processing change |
| Performance | Google Benchmark | Hot path modifications |
| Regression | Custom | Every bug fix |

### Running Tests

```bash
# All tests
ctest --output-on-failure

# Specific test
./build/test/Release/obs-audio-resampler-test.exe

# With memory checking (Linux)
valgrind --leak-check=full ./build/test/Release/obs-test
```

### Audio Testing Standards

- **Sample-accurate**: Compare output buffers sample-by-sample
- **Golden master**: Reference outputs stored in `test/data/audio/`
- **Property-based**: Use `rapidcheck` for parameter space exploration
- **Real-time stress**: Run at 48kHz/96kHz with 128-4096 block sizes

---

## Commit & PR Conventions

### Commit Messages (Conventional Commits)

```
<type>(<scope>): <subject>

<body>

<footer>
```

**Types:**
- `feat`: New feature
- `fix`: Bug fix
- `refactor`: Code change neither fixing nor adding features
- `perf`: Performance improvement
- `docs`: Documentation only
- `test`: Test additions/modifications
- `build`: Build system changes
- `ci`: CI configuration changes
- `chore`: Maintenance, no functional change

**Examples:**
```
feat(win-asio): add dynamic COM driver discovery via registry

fix(obs-vst3): resolve parameter automation race condition

perf(audio): optimize ring buffer with cache-line padding
```

### PR Titles

Same format as commits. PR title = squash commit message.

### Sign-Off

All commits must be signed (`git commit -s`):
```
Signed-off-by: Your Name <you@example.com>
```

---

## AI-Assisted Development

We **welcome** AI-assisted contributions with the same standards as human-written code.

### Guidelines for AI-Assisted Work

1. **You are responsible** — Review every line. The AI is a tool, not an author.
2. **Disclose AI use** — Add `AI-Assisted: <tool/model>` in PR description
3. **No hallucinated APIs** — Verify every function, constant, and type exists
4. **No cargo-cult patterns** — Understand *why* the code works
5. **Test thoroughly** — AI generates plausible bugs; catch them

### Example PR Description

```markdown
## Summary
Add lock-free SPSC ring buffer for ASIO audio transport.

## AI-Assisted
GitHub Copilot (Claude 3.5) — initial implementation + tests

## Testing
- Unit tests: 15 new tests covering push/pop/overflow/underflow
- Stress test: 10M iterations at 48kHz, zero failures
- Benchmark: 2.3ns/op (vs 12ns/op for mutex-based queue)

## Checklist
- [ ] Real-time safe (no allocations, no locks)
- [ ] Cache-line aligned (verified with `perf stat`)
- [ ] Power-of-2 capacity for branchless modulo
- [ ] Atomic operations use correct memory ordering
```

---

## Review Process

### Reviewer Responsibilities

1. **Verify correctness** — Logic, edge cases, error handling
2. **Check performance** — Allocations, lock contention, cache behavior
3. **Validate architecture** — Fits module boundaries, no layering violations
4. **Confirm tests** — Coverage, determinism, meaningful assertions
5. **Read for maintainability** — Names, comments, complexity

### Author Responsibilities

1. **Self-review first** — Catch obvious issues before requesting review
2. **Respond promptly** — Address comments within 24 hours
3. **Don't defend bad code** — If reviewer is right, fix it
4. **Explain non-obvious choices** — Comments in code > PR discussion

### Review Timeline

- **First review**: Within 48 hours of PR ready
- **Follow-up**: Within 24 hours of author response
- **Merge**: After 2 approvals (1 for trivial fixes), all CI green

---

## Architecture Decisions

### When to Write an ADR

Create an Architecture Decision Record (`docs/adr/NNNN-title.md`) for:
- New plugin or subsystem
- Cross-cutting concern (threading, memory, logging)
- External dependency adoption
- Breaking API changes

### ADR Template

```markdown
# ADR NNNN: Title

## Status
Proposed | Accepted | Superseded

## Context
What problem are we solving?

## Decision
What are we doing?

## Consequences
### Positive
### Negative
### Risks

## Alternatives Considered
1. ...
2. ...

## References
- Links to issues, PRs, external docs
```

---

## Getting Help

- **Technical questions**: GitHub Discussions > Q&A
- **Bug reports**: GitHub Issues (use templates)
- **Security issues**: Email security@obs-community.org
- **Governance**: GitHub Discussions > Governance

---

## Recognition

All contributors (code, docs, tests, reviews, triage, ideas) are listed in `AUTHORS` and release notes. No distinction between "core" and "community"—there is only the community.

---

*Last updated: 2024*
*This document evolves—propose changes via PR.*