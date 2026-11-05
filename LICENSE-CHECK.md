# License Compliance Validation

This document outlines the license compliance validation rules and checks enforced by the CI pipeline for the LibreCast Studio project.

## License Identification

The LibreCast Studio project is licensed under the GNU General Public License v3.0 or later (GPL-3.0-or-later). The full license text is available in the [COPYING](./COPYING) file.

All source code files must include the appropriate license header or be covered by the project's license as defined in the COPYING file.

## Dependency License Scanning

The CI pipeline scans all dependencies for license compatibility. The following rules apply:

1. **Allowed Licenses**: Dependencies must be licensed under licenses that are compatible with GPL-3.0-or-later. This includes, but is not limited to:
   - GPL-3.0-or-later
   - LGPL-3.0-or-later
   - AGPL-3.0-or-later
   - MIT
   - BSD-2-Clause
   - BSD-3-Clause
   - Apache-2.0
   - MPL-2.0

2. **Prohibited Licenses**: Dependencies under the following licenses are prohibited:
   - Any license that is not compatible with GPL-3.0-or-later (e.g., proprietary licenses, licenses that impose additional restrictions beyond GPLv3)
   - Licenses that require attribution in a manner that conflicts with the GPL's terms (if such attribution cannot be satisfied)

3. **Scanning Process**: The pipeline uses automated tools to check the licenses of all dependencies (including transitive dependencies) and fails the build if any prohibited license is detected.

## Project-Specific License Policies

In addition to the general license compliance, the project enforces the following policies:

1. **File Headers**: Every source code file must contain a license header indicating it is licensed under GPL-3.0-or-later, unless the file is explicitly exempted (e.g., third-party files with compatible licenses). The header should be consistent with the project's existing headers.

2. **Contributions**: By contributing to the project, you agree that your contributions will be licensed under GPL-3.0-or-later.

3. **Third-Party Code**: Third-party code included in the repository must be accompanied by its license and must be compatible with GPL-3.0-or-later. The location and license of third-party code must be documented in the `THIRD-PARTY.md` file (if such a file exists) or in the source code comments.

4. **Trademarks**: The project's name and logos may be trademarked. Use of the project's trademarks must comply with the project's trademark policy.

## Validation in CI

The `license-check` job in the CI pipeline (`.github/workflows/validate.yml`) validates the presence and content of this file. It checks for:
- Existence of `LICENSE-CHECK.md` in the repository root.
- Reference to the GPL-3.0-or-later license (as a minimum requirement).

Failure to meet these checks will result in the job failing.

---
*This document is subject to change as the project's license compliance requirements evolve.*