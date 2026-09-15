"""YAML generation-profile serialization and compatibility validation."""

from __future__ import annotations

from collections.abc import Mapping
import pathlib
from typing import Any

import yaml
from packaging.specifiers import InvalidSpecifier, SpecifierSet
from packaging.version import InvalidVersion, Version

from .errors import ProfileError
from .model import (
    DEFAULT_APIS,
    DEFAULT_FORMAT_GENERATED_CODE,
    DEFAULT_LANGUAGE,
    DEFAULT_MODULE_PREFIX,
    DEFAULT_SPLIT_INTO_PORT_DIR,
    DEFAULT_SPLIT_SRC_INC_FILES,
    PROFILE_VERSION_KEY,
    SUPPORTED_APIS,
    GenerationConfig,
)
from .validation import make_generation_config
from .version import __version__


def _profile_version_constraint() -> str:
    """Build the compatibility range written into newly saved profiles."""
    try:
        current = Version(__version__)
    except InvalidVersion as exc:
        raise ProfileError(f"Invalid kiwicgen version: {__version__!r}.") from exc

    upper = Version(f"{current.major + 1}.0.0")
    return f">={current},<{upper}"


def _validate_profile_version_constraint(value: Any) -> None:
    """Require the running kiwicgen version to satisfy the profile constraint."""
    if not isinstance(value, str) or not value.strip():
        raise ProfileError(
            f"Profile field '{PROFILE_VERSION_KEY}' must contain a version constraint."
        )

    try:
        specifier = SpecifierSet(value.strip())
        current = Version(__version__)
    except (InvalidSpecifier, InvalidVersion) as exc:
        raise ProfileError(
            f"Invalid {PROFILE_VERSION_KEY} constraint: {value!r}."
        ) from exc

    if current not in specifier:
        raise ProfileError(
            f"Profile requires kiwicgen {value.strip()}; current version is {__version__}."
        )


def _profile_mapping(config: GenerationConfig) -> dict[str, Any]:
    """Convert a normalized configuration into the stable YAML profile schema."""
    return {
        PROFILE_VERSION_KEY: _profile_version_constraint(),
        "module_prefix": config.module_prefix,
        "ports": list(config.ports),
        "language": config.language,
        "api": {name: name in config.apis for name in SUPPORTED_APIS},
        "layout": {
            "split_into_port_dir": config.split_into_port_dir,
            "split_src_inc_files": config.split_src_inc_files,
        },
        "format_generated_code": config.format_generated_code,
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
    """Validate a mapping-valued YAML field while accepting an omitted value."""
    if value is None:
        return {}
    if not isinstance(value, Mapping):
        raise ProfileError(f"Profile field '{field_name}' must be a mapping.")
    return value


def _require_sequence(value: Any, field_name: str) -> list[Any]:
    """Validate a list-valued YAML field without accepting strings as sequences."""
    if not isinstance(value, list):
        raise ProfileError(f"Profile field '{field_name}' must be a list.")
    return value


def load_profile(path: str | pathlib.Path) -> GenerationConfig:
    """Load, validate and normalize a kiwicgen YAML generation profile."""
    profile_path = pathlib.Path(path)
    if not profile_path.exists():
        raise ProfileError(f"Profile file not found: {profile_path}")

    try:
        with profile_path.open("r", encoding="utf-8") as stream:
            raw = yaml.safe_load(stream)
    except yaml.YAMLError as exc:
        raise ProfileError(f"Invalid YAML profile '{profile_path}': {exc}") from exc

    if raw is None:
        raw = {}
    if not isinstance(raw, Mapping):
        raise ProfileError("Profile root must be a mapping.")

    if PROFILE_VERSION_KEY not in raw:
        raise ProfileError(f"Profile field '{PROFILE_VERSION_KEY}' is required.")
    _validate_profile_version_constraint(raw[PROFILE_VERSION_KEY])

    api_raw = _require_mapping(raw.get("api"), "api")
    layout_raw = _require_mapping(raw.get("layout"), "layout")
    ports_raw = _require_sequence(raw.get("ports"), "ports")

    unknown_apis = set(api_raw).difference(SUPPORTED_APIS)
    if unknown_apis:
        names = ", ".join(sorted(str(name) for name in unknown_apis))
        raise ProfileError(f"Profile contains unsupported API group(s): {names}.")

    selected_apis = {
        name
        for name in SUPPORTED_APIS
        if bool(api_raw.get(name, name in DEFAULT_APIS))
    }

    return make_generation_config(
        module_prefix=str(raw.get("module_prefix", DEFAULT_MODULE_PREFIX)),
        ports=[str(port) for port in ports_raw],
        language=str(raw.get("language", DEFAULT_LANGUAGE)),
        apis=selected_apis,
        split_into_port_dir=bool(
            layout_raw.get("split_into_port_dir", DEFAULT_SPLIT_INTO_PORT_DIR)
        ),
        split_src_inc_files=bool(
            layout_raw.get("split_src_inc_files", DEFAULT_SPLIT_SRC_INC_FILES)
        ),
        format_generated_code=bool(
            raw.get("format_generated_code", DEFAULT_FORMAT_GENERATED_CODE)
        ),
    )
