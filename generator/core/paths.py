"""Runtime-resource and output-layout path resolution."""

from __future__ import annotations

import pathlib
import re
import sys

from .errors import ResourceError
from .model import GenerationConfig


GENERATOR_ROOT = pathlib.Path(__file__).resolve().parents[1]
REPOSITORY_ROOT = GENERATOR_ROOT.parent
CLANG_FORMAT_STYLE_FILE = "kiwicgen-clang-format.yaml"


def runtime_root() -> pathlib.Path:
    """Resolve the external runtime root for source and packaged execution."""
    if getattr(sys, "frozen", False):
        return pathlib.Path(sys.executable).resolve().parent
    return REPOSITORY_ROOT


def resolve_templates_dir() -> pathlib.Path:
    """Resolve the current OSAL template set for source and packaged execution."""
    if getattr(sys, "frozen", False):
        path = runtime_root() / "templates" / "osal"
    else:
        path = GENERATOR_ROOT / "resources" / "templates" / "osal"

    if path.exists() and path.is_dir():
        return path

    raise ResourceError(f"OSAL template directory not found: {path}")


def resolve_formatter_style() -> pathlib.Path:
    """Resolve the formatter policy owned and distributed by kiwicgen."""
    if getattr(sys, "frozen", False):
        path = runtime_root() / CLANG_FORMAT_STYLE_FILE
    else:
        path = GENERATOR_ROOT / "resources" / CLANG_FORMAT_STYLE_FILE

    if not path.exists() or not path.is_file():
        raise ResourceError(f"kiwicgen clang-format style file not found: {path}")
    if not path.read_text(encoding="utf-8").strip():
        raise ResourceError(f"clang-format style file is empty: {path}")
    return path.resolve()


def resolve_app_asset(relative_path: str) -> pathlib.Path:
    """Resolve one GUI/distribution asset without exposing packaging details."""
    return runtime_root() / relative_path


def resolve_project_readme() -> pathlib.Path:
    """Resolve the project README copied beside generated output when available."""
    return runtime_root() / "README.md"


def base_header_dir(module_dir: pathlib.Path, config: GenerationConfig) -> pathlib.Path:
    """Resolve the generic header destination for the selected layout."""
    return module_dir / "include" if config.split_src_inc_files else module_dir


def base_source_dir(module_dir: pathlib.Path, config: GenerationConfig) -> pathlib.Path:
    """Resolve the generic source destination for the selected layout."""
    return module_dir / "src" if config.split_src_inc_files else module_dir


def port_slug(port: str) -> str:
    """Return the stable lowercase filesystem/CMake suffix for one port."""
    return re.sub(r"[^a-z0-9]+", "_", port.strip().lower()).strip("_")


def port_root_dir(
    module_dir: pathlib.Path,
    config: GenerationConfig,
    port: str,
) -> pathlib.Path:
    """Resolve the backend root without duplicating layout rules in frontends."""
    if config.split_into_port_dir:
        return module_dir / "portable" / port_slug(port)
    return module_dir


def port_header_dir(
    module_dir: pathlib.Path,
    config: GenerationConfig,
    port: str,
) -> pathlib.Path:
    """Resolve the backend header destination for the selected layout."""
    root = port_root_dir(module_dir, config, port)
    return root / "include" if config.split_src_inc_files else root


def port_source_dir(
    module_dir: pathlib.Path,
    config: GenerationConfig,
    port: str,
) -> pathlib.Path:
    """Resolve the backend source destination for the selected layout."""
    root = port_root_dir(module_dir, config, port)
    return root / "src" if config.split_src_inc_files else root
