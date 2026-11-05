#!/usr/bin/env python3
"""Real-time safety audit tool for C++ source files.

Scans source files whose relative path contains one of the RT-thread
substrings (audio, utility, render) for forbidden real-time-violating
constructs: new, malloc, delete, mutex, std::vector, std::string.

This gate mirrors the structure of tools/rt_invariant_gate.py:
  - Repo-root resolution: NBS_ROOT env -> positional arg -> script-parent fallback
  - Plain text output with a stable [G7] tag prefix, one line per finding
  - Exit code 0 = PASS, 1 = FAIL (violation found)

Usage:
    python tools/rt_safety_audit.py [root_dir] [--extensions .cpp,.h]
    NBS_ROOT=/path/to/repo python tools/rt_safety_audit.py
"""

import os
import re
import sys
from pathlib import Path

# RT-thread path substrings: a file is in-scope if any of these appears
# as a path component anywhere in its path relative to the repo root.
# This matches both top-level dirs (audio/) and nested paths
# (src/audio/, frontend/utility/, render/effects/) per the spec:
# "files whose paths contain those substrings".
RT_THREAD_SUBSTRINGS: tuple[str, ...] = ("audio", "render")

# Non-RT directories that happen to contain files or subdirectories with
# matching names (e.g. Qt frontend UI helpers, control-plane signaling, tests).
NON_RT_PREFIXES: tuple[str, ...] = (
    "frontend/",
    "src/network/",
    "plugins/",
    "test",
    "tests/",
    "benchmarks/",
    "spikes/",
    "example/",
    "third_party/",
    "deps/",
)

# Forbidden real-time-violating constructs: (compiled_regex, reason).
FORBIDDEN_PATTERNS: list[tuple[re.Pattern, str]] = [
    (re.compile(r"\bnew\b"), "heap allocation (new)"),
    (re.compile(r"\bmalloc\b"), "heap allocation (malloc)"),
    (re.compile(r"\bdelete\b"), "heap free (delete)"),
    (re.compile(r"\bmutex\b"), "locking primitive (mutex)"),
    (re.compile(r"std::vector"), "heap-backed container (std::vector)"),
    (re.compile(r"std::string"), "heap-backed container (std::string)"),
]

# Default C/C++ source extensions.
DEFAULT_EXTENSIONS: set[str] = {".cpp", ".c", ".h", ".hpp", ".cc", ".cxx", ".hxx"}

# Tag prefix for all output lines, matching the NBS rt_invariant_gate.py
# [G7] convention so CI logs are grep-able and self-filtering.
TAG = "[G7]"

# Directories to skip during the tree walk (VCS, build artifacts, deps).
SKIP_DIRS = {
    ".git", ".svn", "build", "build.bak", "build4",
    "build_libsrt", "build_task", "build_verify",
    "build_x64", ".worktrees", ".deps",
}


class Violation:
    """A single RT-safety violation."""
    __slots__ = ("file", "line", "reason", "source")

    def __init__(self, file: str, line: int, reason: str, source: str):
        self.file = file
        self.line = line
        self.reason = reason
        self.source = source

    def __repr__(self) -> str:
        return f"{self.file}:{self.line}  {self.reason}  | {self.source}"


def is_rt_thread_path(rel_path: str) -> bool:
    """Return True if the file's relative path contains any RT-thread
    substring as a path component (e.g. 'src/audio/', 'render/util/')."""
    norm = rel_path.replace("\\", "/")
    if any(norm.startswith(p) for p in NON_RT_PREFIXES):
        return False
    parts = Path(rel_path).parts
    return any(sub in parts for sub in RT_THREAD_SUBSTRINGS)


def scan_file(full_path: Path, rel_path: str, extensions: set[str]) -> list[Violation]:
    """Scan a single file for forbidden RT patterns.

    Returns a list of Violation objects. Files whose extension is not in
    `extensions` or whose path does not contain an RT-thread substring are
    skipped. Access errors are silently ignored.
    """
    if full_path.suffix.lower() not in extensions:
        return []
    if not is_rt_thread_path(rel_path):
        return []

    violations: list[Violation] = []
    try:
        content = full_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return violations

    lines = content.splitlines()
    for line_num, line in enumerate(lines, start=1):
        code = line.split("//")[0]  # strip line comments (rt_invariant_gate.py convention)
        for rx, reason in FORBIDDEN_PATTERNS:
            if rx.search(code):
                violations.append(Violation(rel_path, line_num, reason, line.strip()))
    return violations


def scan_tree(root: Path, extensions: set[str]) -> list[Violation]:
    """Walk the tree under root, scanning every file whose relative path
    contains an RT-thread substring (audio, utility, render) and has a
    matching extension. Skips VCS/build/deps directories."""
    all_violations: list[Violation] = []
    for dirpath, dirnames, filenames in os.walk(root, followlinks=False):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS and not d.startswith("build_")]
        for fname in filenames:
            full_path = Path(dirpath) / fname
            try:
                rel_path = os.path.relpath(full_path, root)
            except (ValueError, OSError):
                continue  # cross-mount or inaccessible — skip silently
            violations = scan_file(full_path, rel_path, extensions)
            all_violations.extend(violations)
    return all_violations


def resolve_root(argv: list[str]) -> Path:
    """Resolve repo root: NBS_ROOT env -> positional arg -> script-parent."""
    positional = [a for a in argv[1:] if not a.startswith("--")]
    env_root = os.environ.get("NBS_ROOT")
    if env_root:
        return Path(env_root)
    if positional:
        return Path(positional[0]).resolve()
    return Path(__file__).resolve().parent.parent  # tools/ -> project root


def parse_extensions(argv: list[str]) -> set[str]:
    """Parse an optional --extensions flag (comma or space separated).
    Falls back to DEFAULT_EXTENSIONS if not provided."""
    exts: set[str] = set()
    args = argv[1:]
    i = 0
    while i < len(args):
        arg = args[i]
        if arg == "--extensions" and i + 1 < len(args):
            raw = args[i + 1].split(",")
            for e in raw:
                e = e.strip().lower()
                if e and not e.startswith("."):
                    e = "." + e
                if e:
                    exts.add(e)
            i += 2
        elif arg.startswith("--extensions="):
            raw = arg.split("=", 1)[1].split(",")
            for e in raw:
                e = e.strip().lower()
                if e and not e.startswith("."):
                    e = "." + e
                if e:
                    exts.add(e)
            i += 1
        else:
            i += 1
    return exts or set(DEFAULT_EXTENSIONS)


def main() -> int:
    root = resolve_root(sys.argv)
    extensions = parse_extensions(sys.argv)

    if not root.is_dir():
        print(f"{TAG} RT-safety gate: FAIL - root not a directory: {root}", file=sys.stderr)
        return 1

    violations = scan_tree(root, extensions)

    if violations:
        print(f"{TAG} RT-safety gate: FAILED - forbidden RT construct(s) in "
              f"audio/utility/render thread paths:")
        for v in violations:
            print(f"  VIOLATION {v}")
        print(f"{TAG} {len(violations)} violation(s). Build rejected.")
        return 1
    else:
        print(f"{TAG} RT-safety gate: 0 violations across RT thread paths. PASS")
        return 0


if __name__ == "__main__":
    sys.exit(main())
