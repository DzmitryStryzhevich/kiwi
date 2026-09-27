"""Resolve clang-format resources without depending on generator frontends/core."""

from __future__ import annotations

import pathlib
import shutil
import sys


class FormatterError(RuntimeError):
    """Raised when clang-format resources or execution cannot be used."""


def resolve_clang_format() -> pathlib.Path:
    """Resolve clang-format for source execution and standalone distributions."""
    executable_name = "clang-format.exe" if sys.platform == "win32" else "clang-format"

    if getattr(sys, "frozen", False):
        executable = pathlib.Path(sys.executable).resolve().parent / "tools" / executable_name
        if executable.exists() and executable.is_file():
            return executable
        raise FormatterError(f"Bundled clang-format executable not found: {executable}")

    python_root = pathlib.Path(sys.executable).resolve().parent
    candidates = [
        python_root / executable_name,
        pathlib.Path(sys.prefix) / "Scripts" / executable_name,
        pathlib.Path(sys.prefix) / "bin" / executable_name,
    ]
    path_executable = shutil.which("clang-format")
    if path_executable:
        candidates.append(pathlib.Path(path_executable))

    for candidate in candidates:
        if candidate.exists() and candidate.is_file():
            return candidate

    raise FormatterError(
        "clang-format was not found. Install kiwicgen source dependencies before generation."
    )


def resolve_clang_format_style(path: pathlib.Path) -> pathlib.Path:
    """Validate and resolve the style policy selected by the generation core."""
    if not path.exists() or not path.is_file():
        raise FormatterError(f"kiwicgen clang-format style file not found: {path}")
    if not path.read_text(encoding="utf-8").strip():
        raise FormatterError(f"clang-format style file is empty: {path}")
    return path.resolve()
