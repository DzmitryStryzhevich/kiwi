"""Post-generation clang-format execution."""

from __future__ import annotations

from collections.abc import Callable, Iterable
import pathlib
import subprocess
import sys
from typing import Any

from .resolver import FormatterError, resolve_clang_format, resolve_clang_format_style


def _ensure_final_newline(content: str) -> str:
    """Normalize generated text to exactly one final newline."""
    return content.rstrip("\r\n") + "\n"


def format_generated_sources(
    generated: Iterable[pathlib.Path],
    log: Callable[[str], None],
    style_path: pathlib.Path,
) -> None:
    """Apply the fixed kiwicgen formatting policy to generated C source files."""
    source_files = [path for path in generated if path.suffix.lower() in {".c", ".h"}]
    if not source_files:
        return

    executable = resolve_clang_format()
    resolved_style = resolve_clang_format_style(style_path)

    subprocess_options: dict[str, Any] = {
        "check": True,
        "capture_output": True,
        "text": True,
    }
    if sys.platform == "win32":
        subprocess_options["creationflags"] = subprocess.CREATE_NO_WINDOW

    for path in source_files:
        log(f"[INFO] Formatting: {path}")

        try:
            subprocess.run(
                [
                    str(executable),
                    f"--style=file:{resolved_style}",
                    "-i",
                    str(path),
                ],
                **subprocess_options,
            )
        except (OSError, subprocess.CalledProcessError) as exc:
            stderr = getattr(exc, "stderr", "") or ""
            details = stderr.strip() or str(exc)
            raise FormatterError(
                f"Failed to format generated file '{path}': {details}"
            ) from exc

        path.write_text(
            _ensure_final_newline(path.read_text(encoding="utf-8")),
            encoding="utf-8",
            newline="\n",
        )
