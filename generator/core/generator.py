"""Shared orchestration pipeline for OSAL code generation."""

from __future__ import annotations

from collections.abc import Callable
import pathlib
import shutil

from formatter.formatter import format_generated_sources
from formatter.resolver import FormatterError

from .errors import FormattingError, TemplateError
from .model import PORT_TEMPLATE_DIRS, GenerationConfig
from .paths import (
    base_header_dir,
    base_source_dir,
    port_header_dir,
    port_root_dir,
    port_slug,
    port_source_dir,
    resolve_formatter_style,
    resolve_project_readme,
    resolve_templates_dir,
)
from .template_renderer import (
    apply_api_profile_markers,
    apply_prefix,
    render_base_cmake,
    render_combined_cmake,
    render_port_cmake,
    render_profile_header,
)
from .validation import build_prefix_forms, validate_generation_support


def _ensure_final_newline(content: str) -> str:
    """Normalize generated text to exactly one final newline."""
    return content.rstrip("\r\n") + "\n"


def _read_template(path: pathlib.Path) -> str:
    """Read one required UTF-8 template with a kiwicgen-specific error."""
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        raise TemplateError(f"Unable to read template '{path}': {exc}") from exc


def _write_text(
    path: pathlib.Path,
    content: str,
    generated: list[pathlib.Path],
    log: Callable[[str], None],
) -> None:
    """Write one deterministic text artifact and register it for post-processing."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(_ensure_final_newline(content), encoding="utf-8", newline="\n")
    generated.append(path)
    log(f"[INFO] Generated: {path}")


def generate(
    config: GenerationConfig,
    output_root: str | pathlib.Path,
    *,
    log_callback: Callable[[str], None] | None = None,
) -> list[pathlib.Path]:
    """Run the complete OSAL generation pipeline for one normalized configuration."""
    validate_generation_support(config)

    log = log_callback or (lambda _message: None)
    log("[STEP] Resolving generation configuration and resources")
    forms = build_prefix_forms(config.module_prefix)
    templates_dir = resolve_templates_dir()
    output_root_path = pathlib.Path(output_root)
    module_dir = output_root_path / forms.snake
    module_dir.mkdir(parents=True, exist_ok=True)

    selected = frozenset(config.apis)
    generated: list[pathlib.Path] = []

    log("[STEP] Rendering OSAL templates")
    template_outputs = [
        (
            templates_dir / "template_osal.h",
            base_header_dir(module_dir, config) / f"{forms.snake}_osal.h",
        ),
        (
            templates_dir / "template_osal.c",
            base_source_dir(module_dir, config) / f"{forms.snake}_osal.c",
        ),
    ]

    for source, destination in template_outputs:
        text = _read_template(source)
        text = apply_api_profile_markers(text, selected)
        text = apply_prefix(text, forms)
        _write_text(destination, text, generated, log)

    for port in config.ports:
        template_dir_name = PORT_TEMPLATE_DIRS[port]
        slug = port_slug(port)
        log(f"[INFO] Rendering port: {port}")

        port_outputs = [
            (
                templates_dir / "portable" / template_dir_name / f"template_osal_{slug}.h",
                port_header_dir(module_dir, config, port) / f"{forms.snake}_osal_{slug}.h",
            ),
            (
                templates_dir / "portable" / template_dir_name / f"template_osal_{slug}.c",
                port_source_dir(module_dir, config, port) / f"{forms.snake}_osal_{slug}.c",
            ),
        ]

        for source, destination in port_outputs:
            text = _read_template(source)
            text = apply_api_profile_markers(text, selected)
            text = apply_prefix(text, forms)
            _write_text(destination, text, generated, log)

    log("[STEP] Rendering OSAL profile")
    profile_path = base_header_dir(module_dir, config) / f"{forms.snake}_osal_profile.h"
    _write_text(
        profile_path,
        render_profile_header(forms, selected, config.ports),
        generated,
        log,
    )

    log("[STEP] Rendering build-system files")
    root_cmake = module_dir / "CMakeLists.txt"
    if config.split_into_port_dir and not config.split_src_inc_files:
        base_cmake = apply_prefix(_read_template(templates_dir / "CMakeLists.txt"), forms)
        _write_text(root_cmake, base_cmake, generated, log)
        for port in config.ports:
            template_dir_name = PORT_TEMPLATE_DIRS[port]
            port_cmake_text = apply_prefix(
                _read_template(
                    templates_dir
                    / "portable"
                    / template_dir_name
                    / "CMakeLists.txt"
                ),
                forms,
            )
            port_cmake = port_root_dir(module_dir, config, port) / "CMakeLists.txt"
            _write_text(port_cmake, port_cmake_text, generated, log)
    elif config.split_into_port_dir:
        _write_text(root_cmake, render_base_cmake(forms, config), generated, log)
        for port in config.ports:
            port_cmake = port_root_dir(module_dir, config, port) / "CMakeLists.txt"
            _write_text(
                port_cmake,
                render_port_cmake(forms, config, port),
                generated,
                log,
            )
    else:
        _write_text(root_cmake, render_combined_cmake(forms, config), generated, log)

    if config.format_generated_code:
        log("[STEP] Formatting generated code")
        try:
            format_generated_sources(generated, log, resolve_formatter_style())
        except FormatterError as exc:
            raise FormattingError(str(exc)) from exc
        log("[ OK ] Generated code formatted")

    log("[STEP] Finalizing generated output")
    readme = resolve_project_readme()
    if readme.exists():
        output_root_path.mkdir(parents=True, exist_ok=True)
        shutil.copy2(readme, output_root_path / "kiwicgen-README.md")

    log(f"[ OK ] Generation completed: {module_dir}")
    return generated
