# MANIFESTO.md

# Community-First Independent OBS Studio Hard Fork — Founding Manifesto

---

## Preamble

We, the contributors of the Community-First Independent OBS Studio Hard Fork, declare this project's existence as a necessary evolution of open-source broadcasting software. We believe that the tools of digital expression must belong to those who wield them—not to corporate stewards, not to gatekeepers, not to interests that prioritize metrics over makers.

This manifesto codifies the principles that govern our work, our community, and our code.

---

## Pillar I: Stability as a Moral Obligation

> **Broadcasters cannot debug their tools while live.**

Every line of code in the hot path—every audio callback, every frame encoder, every network packet—must be written with the understanding that someone, somewhere, is depending on it to *not fail*. We reject "move fast and break things" in favor of "move deliberately and verify everything."

**Commitments:**
- Zero compiler warnings on all supported toolchains (MSVC, Clang, GCC)
- Real-time audio threads are allocation-free, lock-free, and bounded
- Comprehensive static analysis integrated into CI
- Regression tests for every reported bug before fix merge
- No feature lands without a migration path for existing users

---

## Pillar II: Zero Contributor Gatekeeping

> **Technical merit is the only credential.**

We do not care about your title, your employer, your follower count, or whether you wrote the patch yourself or collaborated with an AI assistant. We care about:
- Does the code solve a real problem?
- Is it correct, performant, and maintainable?
- Does it respect the architecture and its invariants?

**Commitments:**
- No CLA (Contributor License Agreement) required
- No "core team" veto—decisions by technical consensus
- First-time contributors get the same review depth as veterans
- AI-assisted patches welcomed with same scrutiny as human-written
- Documentation and tests are first-class contributions, not afterthoughts

---

## Pillar III: Active Bug Resolution

> **A known bug is a broken promise.**

We do not accumulate backlogs. We do not label issues "wontfix" because they're "edge cases." If a user encounters a crash, a regression, or a correctness violation, it is a blocker.

**Commitments:**
- Critical bugs (crashes, data loss, audio glitches) addressed within 48 hours
- Every bug gets a root-cause analysis, not a symptom patch
- Regression tests mandatory for every fix
- Public postmortems for user-impacting incidents
- "Stale" bot disabled—issues stay open until resolved or explicitly closed with reasoning

---

## Pillar IV: Transparent Community Stewardship

> **Power corrupts; transparency corrects.**

This project has no benevolent dictator. No single maintainer holds merge keys that others cannot earn. Governance is open, documented, and accountable.

**Commitments:**
- All governance discussions in public forums (GitHub Discussions, not Discord/Slack)
- Roadmap published and updated quarterly with community input
- Release decisions by consensus, not decree
- Financial transparency: any sponsorship/funding disclosed in real-time
- Trademark and branding held by a non-profit entity, not individuals

---

## Pillar V: Pro-Audio as a First-Class Citizen

> **Broadcasters are audio engineers whether they know it or not.**

For too long, broadcast software has treated audio as an afterthought—stereo-only, fixed sample rates, no plugin standard, no professional driver support. We reject this.

**Commitments:**
- Native ASIO support on Windows (not a wrapper, not a hack)
- Native VST3 plugin hosting (full parameter automation, GUI embedding)
- Multi-channel audio throughout the pipeline (up to 64 channels)
- Planar 32-bit float internal format (matching professional DAWs)
- Loudness metering (EBU R128, ATSC A/85) built-in
- Sample-accurate synchronization across audio/video/network

---

## Pillar VI: License as a Shield, Not a Weapon

> **GPLv3 protects users; we use it to protect contributors.**

We upgraded from GPLv2+ to GPLv3+ deliberately. This enables:
- Integration with modern audio SDKs (Steinberg VST3 SDK, ASIO SDK)
- Patent protection for contributors and users
- Anti-tivoization: users can run modified versions on their hardware
- Compatibility with Apache 2.0 dependencies

We will never relicense to a permissive license. We will never accept contributions that require copyright assignment.

---

## Pillar VII: Engineering Over Ego

> **The best idea wins, regardless of source.**

We practice:
- **Doubt-driven development**: Question assumptions before writing code
- **Specification-driven development**: Write the spec, then the code, then the test
- **Systematic debugging**: Understand before fixing; reproduce before claiming fixed
- **Code review as teaching**: Every review is an opportunity to share knowledge
- **Refactoring as hygiene**: Technical debt is a bug with a deadline

---

## The Social Contract

By contributing to this project, you agree to:

1. **Treat every contributor with respect** — especially when disagreeing technically
2. **Argue from evidence**, not authority or identity
3. **Admit when you're wrong** — and celebrate others who do
4. **Document your reasoning** — future you will thank present you
5. **Ship working code** — not perfect code, not clever code, *working* code

---

## Non-Goals (Explicitly Rejected)

- ❌ Feature parity with proprietary broadcast software at the expense of stability
- ❌ Cloud-locked features or mandatory telemetry
- ❌ AI-generated content without human review
- ❌ "Engagement" metrics, gamification, or dark patterns
- ❌ Vendor lock-in (hardware, cloud, platform)
- ❌ Sacrificing audio correctness for "ease of use"

---

## Living Document

This manifesto is not static. It evolves with the project and the community. Proposals to amend are made via the same process as code: open discussion, technical consensus, documented decision.

**Version**: 1.0
**Adopted**: 2024
**Next Review**: Quarterly

---

*"The best time to plant a tree was 20 years ago. The second best time is now. The best time to fork a project is when the upstream loses its way."*

— The Community