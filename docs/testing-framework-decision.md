# Testing Framework Decision for LibreCast-Studio

## Overview
This document evaluates testing framework options for LibreCast-Studio, focusing on backend (C++/Python) and frontend (web UI) testing capabilities, considering the project's broadcast matrix complexity, CI/CD parallelization needs, and team familiarity.

## Evaluated Frameworks

### 1. Postman/Newman + Playwright (Framework 2)
- **Strengths**: 
  - High team familiarity (Postman GUI ubiquitous)
  - Excellent CI/CD integration via Newman CLI
  - Playwright provides robust browser-level validation
- **Weaknesses**:
  - Dual-toolchain synchronization burden (API collections vs Playwright fixtures)
  - Vendor lock-in risk with Postman's proprietary format
  - Limited property-based testing support (no Hypothesis equivalent)
  - Bifurcated community support (GUI-centric Postman vs code-centric Playwright)

### 2. Cypress + API Hybrid (Framework 3)
- **Strengths**:
  - Excellent browser testing coverage and developer experience for web UI
  - Good learning curve for web-specific tasks
- **Weaknesses**:
  - Language fragmentation (mandatory JavaScript/TypeScript) in a C++/Python codebase
  - API testing reliant on brittle workarounds (e.g., `cy.request()`)
  - Hidden maintenance burden from synchronizing UI and API test state
  - Limited CI/CD parallelization due to browser resource consumption
  - Reduced mock service sophistication for complex contract testing
  - Diminished community support for hybrid patterns
  - Lack of native property-based testing constrains protocol fuzzing

### 3. Recommended: pytest + Playwright Hybrid
Given the analysis above, we recommend a hybrid approach using:
- **Backend/API Testing**: pytest (Python) with optional Hypothesis for property-based testing
- **Frontend/UI Testing**: Playwright (Python/TypeScript bindings)
- **Rationale**:
  - **Language Consistency**: Python is already used in LibreCast-Studio tooling and scripts, reducing context switching.
  - **Advanced Testing**: pytest supports fixtures, parametrization, and plugins (e.g., pytest-html, pytest-cov). Hypothesis adds powerful property-based testing for protocol fuzzing—critical for broadcast matrix validation.
  - **CI/CD Parallelization**: pytest executes quickly and scales well in CI; Playwright tests can be parallelized via sharding.
  - **Team Familiarity**: QA engineers can adopt Playwright (similar to Cypress but more flexible); backend engineers are likely already comfortable with pytest.
  - **Reduced Vendor Lock-in**: Open-source, community-driven tools with active ecosystems.
  - **Unified Ecosystem**: Both pytest and Playwright have strong Python bindings, enabling shared utilities and easier maintenance compared to split Postman/Cypress approaches.

## Implementation Plan
1. **Backend**: Establish pytest test suite in `tests/` directory, covering:
   - Core broadcasting logic
   - Audio/video processing pipelines
   - Network protocols (SRT, RTMP, etc.)
   - Use Hypothesis for property-based fuzzing of edge cases.
2. **Frontend**: Implement Playwright tests in `tests/e2e/` for:
   - User OBS-like interactions (stream start/stop, scene switching)
   - Settings persistence
   - Export/import workflows
3. **CI Integration**:
   - Use GitHub Actions to run pytest on push/pull request.
   - Run Playwright tests in separate job with browser dependencies.
   - Employ test sharding for Playwright to improve parallelization.
4. **Documentation**: Update `docs/testing-guidelines.md` with patterns for both backend and frontend tests.

## Conclusion
The pytest + Playwright hybrid addresses the key weaknesses of the evaluated frameworks while leveraging the team's existing strengths. It provides a scalable, maintainable testing solution that supports both unit-level protocol fuzzing and high-fidelity UI validation—essential for LibreCast-Studio's broadcast matrix complexity.

---
*Decision recorded: 2026-10-18*