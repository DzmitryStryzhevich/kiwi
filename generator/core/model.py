"""Frontend-independent data model and stable generator capabilities."""

from __future__ import annotations

from dataclasses import dataclass


PROFILE_VERSION_KEY = "kiwicgen-version"
DEFAULT_MODULE_PREFIX = "foo_module"
DEFAULT_PORTS = ("FreeRTOS",)
DEFAULT_LANGUAGE = "C"
DEFAULT_SPLIT_INTO_PORT_DIR = False
DEFAULT_SPLIT_SRC_INC_FILES = False
DEFAULT_FORMAT_GENERATED_CODE = True

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
SUPPORTED_PORTS = ("FreeRTOS", "POSIX")
IMPLEMENTED_PORTS = frozenset({"FreeRTOS"})
SUPPORTED_LANGUAGES = ("C", "C++")
IMPLEMENTED_LANGUAGES = frozenset({"C"})

PORT_TEMPLATE_DIRS = {
    "FreeRTOS": "freertos",
}


@dataclass(frozen=True)
class PrefixForms:
    """Normalized spellings derived from one user-entered module prefix."""

    upper: str
    camel: str
    pascal: str
    snake: str


@dataclass(frozen=True)
class GenerationConfig:
    """Normalized, frontend-independent kiwicgen generation configuration."""

    module_prefix: str
    ports: tuple[str, ...]
    language: str
    apis: frozenset[str]
    split_into_port_dir: bool
    split_src_inc_files: bool
    format_generated_code: bool
