"""Enforce the project's C++ memory rules.

Plain ``new``, ``delete`` and C allocation are forbidden, so that ownership stays
automatic. Qt widgets are owned by their parents, and ``qt_ownership.hpp`` is the one
place allowed to create them with ``new``.
"""

import re
import sys
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path

ALLOWED_FILES = {"qt_ownership.hpp"}
SOURCE_SUFFIXES = {".cpp", ".hpp", ".h", ".cc"}
SOURCE_FOLDERS = ("src", "app", "tests")

_COMMENTS_AND_STRINGS = re.compile(
    r"""//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'""", re.DOTALL
)
_RULES = (
    (re.compile(r"\bnew\b"), "plain new: use std::make_unique or makeChild"),
    (re.compile(r"(?<!= )\bdelete\b"), "plain delete: ownership must be automatic"),
    (
        re.compile(r"\b(?:malloc|calloc|realloc|free)\s*\("),
        "C allocation: use a standard container",
    ),
)


@dataclass(frozen=True)
class Violation:
    """A forbidden construct in a source file."""

    path: Path
    line: int
    message: str

    def __str__(self) -> str:
        """Return a location that editors can open."""
        return f"{self.path}:{self.line}: {self.message}"


def _blank(match: re.Match[str]) -> str:
    """Remove a comment or string, while keeping line numbers."""
    return "\n" * match.group(0).count("\n")


def find_violations(path: Path, text: str) -> list[Violation]:
    """Return every rule violation in a file's text."""
    if path.name in ALLOWED_FILES:
        return []
    code = _COMMENTS_AND_STRINGS.sub(_blank, text)
    violations = []
    for number, line in enumerate(code.splitlines(), start=1):
        for pattern, message in _RULES:
            if pattern.search(line):
                violations.append(Violation(path, number, message))
    return violations


def source_files(root: Path, folders: Iterable[str] = SOURCE_FOLDERS) -> list[Path]:
    """Return the C++ files to check."""
    return sorted(
        path
        for folder in folders
        for path in (root / folder).rglob("*")
        if path.suffix in SOURCE_SUFFIXES
    )


def main(argv: list[str] | None = None) -> int:
    """Check a repository, and return 1 if any rule is broken."""
    arguments = sys.argv[1:] if argv is None else argv
    root = Path(arguments[0]) if arguments else Path.cwd()
    files = source_files(root)
    violations = [
        violation
        for path in files
        for violation in find_violations(path, path.read_text(encoding="utf-8"))
    ]
    for violation in violations:
        print(violation)
    print(f"Checked {len(files)} C++ files: {len(violations)} problems")
    return 1 if violations else 0


if __name__ == "__main__":  # pragma: no cover
    sys.exit(main())
