"""Configuration normalization and validation shared by all frontends."""

from __future__ import annotations

from collections.abc import Iterable
import re

from .errors import ConfigurationError
from .model import (
    DEFAULT_APIS,
    DEFAULT_FORMAT_GENERATED_CODE,
    DEFAULT_LANGUAGE,
    DEFAULT_MODULE_PREFIX,
    DEFAULT_PORTS,
    DEFAULT_SPLIT_INTO_PORT_DIR,
    DEFAULT_SPLIT_SRC_INC_FILES,
    IMPLEMENTED_LANGUAGES,
    IMPLEMENTED_PORTS,
    SUPPORTED_APIS,
    SUPPORTED_LANGUAGES,
    SUPPORTED_PORTS,
    GenerationConfig,
    PrefixForms,
)


def _to_words(value: str) -> list[str]:
    """Split a user prefix into normalized identifier words."""
    cleaned = re.sub(r"[^A-Za-z0-9]+", "_", value.strip())
    cleaned = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", cleaned)
    return [word for word in cleaned.split("_") if word]


def build_prefix_forms(prefix: str) -> PrefixForms:
    """Derive all identifier spellings used by generated C sources."""
    words = _to_words(prefix)
    if not words:
        raise ConfigurationError("Prefix cannot be empty.")

    lower_words = [word.lower() for word in words]
    upper = "_".join(word.upper() for word in words)
    pascal = "".join(word.capitalize() for word in lower_words)
    camel = lower_words[0] + "".join(word.capitalize() for word in lower_words[1:])
    snake = "_".join(lower_words)
    return PrefixForms(upper=upper, camel=camel, pascal=pascal, snake=snake)


def normalize_port(port: str) -> str:
    """Normalize human-friendly port spellings to one canonical name."""
    normalized = re.sub(r"[^a-z0-9]+", "", port.strip().lower())
    aliases = {
        "freertos": "FreeRTOS",
        "posix": "POSIX",
    }
    try:
        return aliases[normalized]
    except KeyError as exc:
        raise ConfigurationError(
            f"Unsupported port '{port}'. Supported ports: {', '.join(SUPPORTED_PORTS)}."
        ) from exc


def normalize_ports(ports: Iterable[str] | str) -> tuple[str, ...]:
    """Normalize, de-duplicate and preserve the order of selected ports."""
    requested = (ports,) if isinstance(ports, str) else tuple(ports)
    if not requested:
        raise ConfigurationError("At least one port must be selected.")

    normalized: list[str] = []
    for port in requested:
        canonical = normalize_port(str(port))
        if canonical not in normalized:
            normalized.append(canonical)

    return tuple(normalized)


def normalize_language(language: str) -> str:
    """Normalize one target language while retaining planned language names."""
    if not str(language).strip():
        raise ConfigurationError("At least one language must be selected.")

    normalized = re.sub(r"\s+", "", str(language).strip().lower())
    aliases = {
        "c": "C",
        "c++": "C++",
        "cpp": "C++",
        "cxx": "C++",
    }
    try:
        return aliases[normalized]
    except KeyError as exc:
        raise ConfigurationError(
            f"Unsupported language '{language}'. Supported languages: {', '.join(SUPPORTED_LANGUAGES)}."
        ) from exc


def make_generation_config(
    *,
    module_prefix: str = DEFAULT_MODULE_PREFIX,
    ports: Iterable[str] | str = DEFAULT_PORTS,
    language: str = DEFAULT_LANGUAGE,
    apis: Iterable[str] = DEFAULT_APIS,
    split_into_port_dir: bool = DEFAULT_SPLIT_INTO_PORT_DIR,
    split_src_inc_files: bool = DEFAULT_SPLIT_SRC_INC_FILES,
    format_generated_code: bool = DEFAULT_FORMAT_GENERATED_CODE,
) -> GenerationConfig:
    """Validate and normalize configuration received from CLI, YAML or GUI."""
    forms = build_prefix_forms(module_prefix)
    canonical_ports = normalize_ports(ports)
    canonical_language = normalize_language(language)

    selected = frozenset(str(api).strip().lower() for api in apis)
    unsupported = selected.difference(SUPPORTED_APIS)
    if unsupported:
        names = ", ".join(sorted(unsupported))
        raise ConfigurationError(f"Unsupported API group(s): {names}.")

    return GenerationConfig(
        module_prefix=forms.snake,
        ports=canonical_ports,
        language=canonical_language,
        apis=selected,
        split_into_port_dir=bool(split_into_port_dir),
        split_src_inc_files=bool(split_src_inc_files),
        format_generated_code=bool(format_generated_code),
    )


def validate_generation_support(config: GenerationConfig) -> None:
    """Reject supported-but-not-yet-implemented generation targets."""
    unsupported_ports = [port for port in config.ports if port not in IMPLEMENTED_PORTS]
    if unsupported_ports:
        requested = ", ".join(unsupported_ports)
        raise ConfigurationError(f"Port generation is not implemented for: {requested}.")

    if config.language not in IMPLEMENTED_LANGUAGES:
        raise ConfigurationError(
            f"Language generation is not implemented for: {config.language}."
        )
