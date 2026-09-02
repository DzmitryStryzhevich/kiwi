from __future__ import annotations

from dataclasses import dataclass
import pathlib
import re
import shutil
import sys
from collections.abc import Callable, Iterable, Mapping
from typing import Any

import yaml


# The generator lives in <project>/generator. Keep both paths explicit because
# source execution and a PyInstaller bundle resolve resources differently.
ROOT = pathlib.Path(__file__).resolve().parent
PROJECT_ROOT = ROOT.parent

PROFILE_SCHEMA_VERSION = 2
SUPPORTED_PROFILE_SCHEMA_VERSIONS = frozenset({1, PROFILE_SCHEMA_VERSION})
DEFAULT_MODULE_PREFIX = "foo_module"
DEFAULT_PORT = "FreeRTOS"
DEFAULT_SPLIT_INTO_PORT_DIR = False
DEFAULT_SPLIT_SRC_INC_FILES = False

SUPPORTED_APIS = (
    "queue",
    "stream_buffer",
    "lock",
    "semaphore",
    "thread",
    "critical_section",
    "software_timer",
    "time",
    "memory",
)
DEFAULT_APIS = frozenset()
SUPPORTED_PORTS = ("FreeRTOS", "POSIX", "CMSIS RTOS v2")
IMPLEMENTED_PORTS = frozenset({"FreeRTOS"})


class CodegenError(RuntimeError):
    """Raised when the requested code-generation configuration is invalid."""


@dataclass(frozen=True)
class PrefixForms:
    """Normalized spellings derived from one user-entered module prefix."""

    upper: str
    camel: str
    pascal: str
    snake: str


@dataclass(frozen=True)
class GenerationConfig:
    """Normalized, frontend-independent KIWI code-generation configuration."""

    module_prefix: str
    port: str
    apis: frozenset[str]
    split_into_port_dir: bool
    split_src_inc_files: bool


def resolve_templates_dir() -> pathlib.Path:
    """Resolve the OSAL template directory for source and packaged execution."""
    candidates: list[pathlib.Path] = []

    meipass = getattr(sys, "_MEIPASS", None)
    if meipass:
        candidates.append(pathlib.Path(meipass) / "osal")

    candidates.append(PROJECT_ROOT / "osal")
    candidates.append(pathlib.Path.cwd() / "osal")

    for path in candidates:
        if path.exists() and path.is_dir():
            return path

    searched = "\n".join(f" - {path}" for path in candidates)
    raise CodegenError(f"osal directory not found. Checked:\n{searched}")


def _to_words(value: str) -> list[str]:
    cleaned = re.sub(r"[^A-Za-z0-9]+", "_", value.strip())
    cleaned = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", cleaned)
    return [word for word in cleaned.split("_") if word]


def build_prefix_forms(prefix: str) -> PrefixForms:
    """Derive all identifier spellings used by the C templates."""
    words = _to_words(prefix)
    if not words:
        raise CodegenError("Prefix cannot be empty.")

    lower_words = [word.lower() for word in words]
    upper = "_".join(word.upper() for word in words)
    pascal = "".join(word.capitalize() for word in lower_words)
    camel = lower_words[0] + "".join(word.capitalize() for word in lower_words[1:])
    snake = "_".join(lower_words)
    return PrefixForms(upper=upper, camel=camel, pascal=pascal, snake=snake)


def apply_prefix(content: str, forms: PrefixForms) -> str:
    """Replace generic template prefixes in every naming convention in use."""
    mapping = {
        "TEMPLATE": forms.upper,
        "Template": forms.pascal,
        "template": forms.snake,
    }
    for source, destination in mapping.items():
        content = content.replace(source, destination)

    camel_symbol_prefix_pattern = re.compile(
        rf"\b{re.escape(forms.snake)}_osal(?=[A-Z])"
    )
    return camel_symbol_prefix_pattern.sub(f"{forms.camel}_osal", content)


def apply_api_profile_markers(content: str, selected_apis: set[str] | frozenset[str]) -> str:
    """Keep enabled template sections and remove disabled API groups."""
    marker_to_api = {
        "QUEUE": "queue",
        "STREAM_BUFFER": "stream_buffer",
        "LOCK": "lock",
        "SEMAPHORE": "semaphore",
        "THREAD": "thread",
        "CRITICAL_SECTION": "critical_section",
        "SOFTWARE_TIMER": "software_timer",
        "TIME": "time",
        "MEMORY": "memory",
    }

    for marker, api_name in marker_to_api.items():
        pattern = re.compile(
            rf"(?ms)^[ \t]*//[ \t]*BEGIN[ \t]+{marker}[ \t]*\n"
            rf"(.*?)"
            rf"^[ \t]*//[ \t]*END[ \t]+{marker}[ \t]*\n?"
        )
        if api_name in selected_apis:
            content = pattern.sub(r"\1", content)
        else:
            content = pattern.sub("", content)

    return content


def render_profile_header(forms: PrefixForms, selected_apis: frozenset[str], port: str) -> str:
    """Render the generated C profile header for the selected OSAL API set."""
    upper = forms.upper
    guard = f"{upper}_OSAL_PROFILE_H_"
    port_macro = re.sub(r"[^A-Za-z0-9]+", "_", port).upper().strip("_")

    flags = {
        "queue": "QUEUE",
        "stream_buffer": "STREAM_BUFFER",
        "lock": "LOCK",
        "semaphore": "SEMAPHORE",
        "thread": "THREAD",
        "critical_section": "CRITICAL_SECTION",
        "software_timer": "SOFTWARE_TIMER",
        "time": "TIME",
        "memory": "MEMORY",
    }

    lines = [
        "/*",
        " * SPDX-License-Identifier: MIT",
        " * Copyright (c) 2026 Kiwi contributors",
        " */",
        "",
        f"#ifndef {guard}",
        f"#define {guard}",
        "",
        "/* Auto-generated by OSAL Code Generator. */",
        f"#define {upper}_OSAL_PORT_{port_macro}    1",
        "",
    ]

    for key, name in flags.items():
        value = 1 if key in selected_apis else 0
        lines.append(f"#define {upper}_OSAL_USE_{name}    {value}")

    lines.extend(["", f"#endif /* {guard} */", ""])
    return "\n".join(lines)


def normalize_port(port: str) -> str:
    """Normalize human-friendly port spellings to one canonical name."""
    normalized = re.sub(r"[^a-z0-9]+", "", port.strip().lower())
    aliases = {
        "freertos": "FreeRTOS",
        "posix": "POSIX",
        "cmsisrtosv2": "CMSIS RTOS v2",
        "cmsisrtos2": "CMSIS RTOS v2",
    }
    try:
        return aliases[normalized]
    except KeyError as exc:
        raise CodegenError(
            f"Unsupported port '{port}'. Supported ports: {', '.join(SUPPORTED_PORTS)}."
        ) from exc


def make_generation_config(
    *,
    module_prefix: str = DEFAULT_MODULE_PREFIX,
    port: str = DEFAULT_PORT,
    apis: Iterable[str] = DEFAULT_APIS,
    split_into_port_dir: bool = DEFAULT_SPLIT_INTO_PORT_DIR,
    split_src_inc_files: bool = DEFAULT_SPLIT_SRC_INC_FILES,
) -> GenerationConfig:
    """Validate and normalize configuration received from CLI, YAML or UI."""
    forms = build_prefix_forms(module_prefix)
    canonical_port = normalize_port(port)

    selected = frozenset(str(api).strip().lower() for api in apis)
    unsupported = selected.difference(SUPPORTED_APIS)
    if unsupported:
        names = ", ".join(sorted(unsupported))
        raise CodegenError(f"Unsupported API group(s): {names}.")

    return GenerationConfig(
        module_prefix=forms.snake,
        port=canonical_port,
        apis=selected,
        split_into_port_dir=bool(split_into_port_dir),
        split_src_inc_files=bool(split_src_inc_files),
    )


def _profile_mapping(config: GenerationConfig) -> dict[str, Any]:
    """Convert a normalized configuration into the stable YAML profile schema."""
    return {
        "kiwi_profile_version": PROFILE_SCHEMA_VERSION,
        "module_prefix": config.module_prefix,
        "port": config.port,
        "api": {name: name in config.apis for name in SUPPORTED_APIS},
        "layout": {
            "split_into_port_dir": config.split_into_port_dir,
            "split_src_inc_files": config.split_src_inc_files,
        },
    }


def save_profile(path: str | pathlib.Path, config: GenerationConfig) -> pathlib.Path:
    """Save the normalized generation configuration as a reusable YAML profile."""
    profile_path = pathlib.Path(path)
    profile_path.parent.mkdir(parents=True, exist_ok=True)
    with profile_path.open("w", encoding="utf-8", newline="\n") as stream:
        yaml.safe_dump(
            _profile_mapping(config),
            stream,
            sort_keys=False,
            default_flow_style=False,
            allow_unicode=True,
        )
    return profile_path


def _require_mapping(value: Any, field_name: str) -> Mapping[str, Any]:
    if value is None:
        return {}
    if not isinstance(value, Mapping):
        raise CodegenError(f"Profile field '{field_name}' must be a mapping.")
    return value


def load_profile(path: str | pathlib.Path) -> GenerationConfig:
    """Load, validate and normalize a KIWI YAML code-generation profile."""
    profile_path = pathlib.Path(path)
    if not profile_path.exists():
        raise CodegenError(f"Profile file not found: {profile_path}")

    try:
        with profile_path.open("r", encoding="utf-8") as stream:
            raw = yaml.safe_load(stream)
    except yaml.YAMLError as exc:
        raise CodegenError(f"Invalid YAML profile '{profile_path}': {exc}") from exc

    if raw is None:
        raw = {}
    if not isinstance(raw, Mapping):
        raise CodegenError("Profile root must be a mapping.")

    version = raw.get("kiwi_profile_version")
    if version not in SUPPORTED_PROFILE_SCHEMA_VERSIONS:
        supported_versions = ", ".join(
            str(item) for item in sorted(SUPPORTED_PROFILE_SCHEMA_VERSIONS)
        )
        raise CodegenError(
            f"Unsupported KIWI profile version: {version!r}. "
            f"Supported versions: {supported_versions}."
        )

    api_raw = _require_mapping(raw.get("api"), "api")
    layout_raw = _require_mapping(raw.get("layout"), "layout")

    unknown_apis = set(api_raw).difference(SUPPORTED_APIS)
    if unknown_apis:
        names = ", ".join(sorted(str(name) for name in unknown_apis))
        raise CodegenError(f"Profile contains unsupported API group(s): {names}.")

    selected_apis = {
        name
        for name in SUPPORTED_APIS
        if bool(api_raw.get(name, name in DEFAULT_APIS))
    }

    return make_generation_config(
        module_prefix=str(raw.get("module_prefix", DEFAULT_MODULE_PREFIX)),
        port=str(raw.get("port", DEFAULT_PORT)),
        apis=selected_apis,
        split_into_port_dir=bool(
            layout_raw.get("split_into_port_dir", DEFAULT_SPLIT_INTO_PORT_DIR)
        ),
        split_src_inc_files=bool(
            layout_raw.get("split_src_inc_files", DEFAULT_SPLIT_SRC_INC_FILES)
        ),
    )


def _base_header_dir(module_dir: pathlib.Path, config: GenerationConfig) -> pathlib.Path:
    return module_dir / "include" if config.split_src_inc_files else module_dir


def _base_source_dir(module_dir: pathlib.Path, config: GenerationConfig) -> pathlib.Path:
    return module_dir / "src" if config.split_src_inc_files else module_dir


def _port_root_dir(module_dir: pathlib.Path, config: GenerationConfig) -> pathlib.Path:
    if config.split_into_port_dir:
        return module_dir / "portable" / "freertos"
    return module_dir


def _port_header_dir(module_dir: pathlib.Path, config: GenerationConfig) -> pathlib.Path:
    root = _port_root_dir(module_dir, config)
    return root / "include" if config.split_src_inc_files else root


def _port_source_dir(module_dir: pathlib.Path, config: GenerationConfig) -> pathlib.Path:
    root = _port_root_dir(module_dir, config)
    return root / "src" if config.split_src_inc_files else root


def _render_base_cmake(forms: PrefixForms, config: GenerationConfig) -> str:
    source = f"src/{forms.snake}_osal.c" if config.split_src_inc_files else f"{forms.snake}_osal.c"
    include_dir = "${CMAKE_CURRENT_LIST_DIR}/include" if config.split_src_inc_files else "${CMAKE_CURRENT_LIST_DIR}"
    return "\n".join(
        [
            "# SPDX-License-Identifier: MIT",
            "# Copyright (c) 2026 Kiwi contributors",
            "",
            "# Base OSAL generated build script.",
            "",
            f"add_library({forms.snake}_osal STATIC",
            f"    {source}",
            ")",
            "",
            f"target_include_directories({forms.snake}_osal",
            "    PUBLIC",
            f"        {include_dir}",
            ")",
            "",
        ]
    )


def _render_port_cmake(forms: PrefixForms, config: GenerationConfig) -> str:
    source = (
        f"src/{forms.snake}_osal_freertos.c"
        if config.split_src_inc_files
        else f"{forms.snake}_osal_freertos.c"
    )
    port_include = "${CMAKE_CURRENT_LIST_DIR}/include" if config.split_src_inc_files else "${CMAKE_CURRENT_LIST_DIR}"
    base_include = (
        "${CMAKE_CURRENT_LIST_DIR}/../../include"
        if config.split_src_inc_files
        else "${CMAKE_CURRENT_LIST_DIR}/../.."
    )
    return "\n".join(
        [
            "# SPDX-License-Identifier: MIT",
            "# Copyright (c) 2026 Kiwi contributors",
            "",
            "# FreeRTOS OSAL port generated build script.",
            "# FreeRTOS include paths and libraries are expected from the parent project.",
            "",
            f"add_library({forms.snake}_osal_freertos STATIC",
            f"    {source}",
            ")",
            "",
            f"target_include_directories({forms.snake}_osal_freertos",
            "    PUBLIC",
            f"        {port_include}",
            f"        {base_include}",
            ")",
            "",
            f"target_link_libraries({forms.snake}_osal_freertos",
            "    PUBLIC",
            f"        {forms.snake}_osal",
            ")",
            "",
        ]
    )


def _render_combined_cmake(forms: PrefixForms, config: GenerationConfig) -> str:
    base_source = f"src/{forms.snake}_osal.c" if config.split_src_inc_files else f"{forms.snake}_osal.c"
    port_source = (
        f"src/{forms.snake}_osal_freertos.c"
        if config.split_src_inc_files
        else f"{forms.snake}_osal_freertos.c"
    )
    include_dir = "${CMAKE_CURRENT_LIST_DIR}/include" if config.split_src_inc_files else "${CMAKE_CURRENT_LIST_DIR}"
    return "\n".join(
        [
            "# SPDX-License-Identifier: MIT",
            "# Copyright (c) 2026 Kiwi contributors",
            "",
            "# Combined base + FreeRTOS OSAL generated build script.",
            "# FreeRTOS include paths and libraries are expected from the parent project.",
            "",
            f"add_library({forms.snake}_osal STATIC",
            f"    {base_source}",
            ")",
            "",
            f"target_include_directories({forms.snake}_osal",
            "    PUBLIC",
            f"        {include_dir}",
            ")",
            "",
            f"add_library({forms.snake}_osal_freertos STATIC",
            f"    {port_source}",
            ")",
            "",
            f"target_include_directories({forms.snake}_osal_freertos",
            "    PUBLIC",
            f"        {include_dir}",
            ")",
            "",
            f"target_link_libraries({forms.snake}_osal_freertos",
            "    PUBLIC",
            f"        {forms.snake}_osal",
            ")",
            "",
        ]
    )


def _write_text(path: pathlib.Path, content: str, generated: list[pathlib.Path], log: Callable[[str], None]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content, encoding="utf-8", newline="\n")
    generated.append(path)
    log(f"Generated: {path}")


def generate(
    config: GenerationConfig,
    output_root: str | pathlib.Path,
    *,
    log_callback: Callable[[str], None] | None = None,
) -> list[pathlib.Path]:
    """Generate OSAL files from one normalized configuration."""
    if config.port not in IMPLEMENTED_PORTS:
        raise CodegenError(
            f"Currently only FreeRTOS port generation is implemented; requested {config.port}."
        )

    log = log_callback or (lambda _message: None)
    forms = build_prefix_forms(config.module_prefix)
    templates_dir = resolve_templates_dir()
    output_root_path = pathlib.Path(output_root)
    module_dir = output_root_path / forms.snake
    module_dir.mkdir(parents=True, exist_ok=True)

    selected = frozenset(config.apis)
    generated: list[pathlib.Path] = []

    template_outputs = [
        (
            templates_dir / "template_osal.h",
            _base_header_dir(module_dir, config) / f"{forms.snake}_osal.h",
        ),
        (
            templates_dir / "template_osal.c",
            _base_source_dir(module_dir, config) / f"{forms.snake}_osal.c",
        ),
        (
            templates_dir / "portable" / "freertos" / "template_osal_freertos.h",
            _port_header_dir(module_dir, config) / f"{forms.snake}_osal_freertos.h",
        ),
        (
            templates_dir / "portable" / "freertos" / "template_osal_freertos.c",
            _port_source_dir(module_dir, config) / f"{forms.snake}_osal_freertos.c",
        ),
    ]

    for source, destination in template_outputs:
        text = source.read_text(encoding="utf-8")
        text = apply_api_profile_markers(text, selected)
        text = apply_prefix(text, forms)
        _write_text(destination, text, generated, log)

    profile_path = _base_header_dir(module_dir, config) / f"{forms.snake}_osal_profile.h"
    _write_text(
        profile_path,
        render_profile_header(forms, selected, config.port),
        generated,
        log,
    )

    root_cmake = module_dir / "CMakeLists.txt"
    if config.split_into_port_dir and not config.split_src_inc_files:
        # Preserve the historical default output byte-for-byte by continuing to
        # use the existing CMake templates when no new layout transformation is requested.
        base_cmake = apply_prefix(
            (templates_dir / "CMakeLists.txt").read_text(encoding="utf-8"),
            forms,
        )
        port_cmake_text = apply_prefix(
            (templates_dir / "portable" / "freertos" / "CMakeLists.txt").read_text(encoding="utf-8"),
            forms,
        )
        _write_text(root_cmake, base_cmake, generated, log)
        port_cmake = _port_root_dir(module_dir, config) / "CMakeLists.txt"
        _write_text(port_cmake, port_cmake_text, generated, log)
    elif config.split_into_port_dir:
        _write_text(root_cmake, _render_base_cmake(forms, config), generated, log)
        port_cmake = _port_root_dir(module_dir, config) / "CMakeLists.txt"
        _write_text(port_cmake, _render_port_cmake(forms, config), generated, log)
    else:
        _write_text(root_cmake, _render_combined_cmake(forms, config), generated, log)

    readme = PROJECT_ROOT / "README.md"
    if readme.exists():
        output_root_path.mkdir(parents=True, exist_ok=True)
        shutil.copy2(readme, output_root_path / "README_generator.md")

    return generated
