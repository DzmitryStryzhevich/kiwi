"""Command-line frontend for the shared ``kiwicgen_core`` generation pipeline."""

from __future__ import annotations

import argparse
import os
import os
import pathlib
import sys

from colorama import Fore, Style, just_fix_windows_console

from colorama import Fore, Style, just_fix_windows_console

import kiwicgen_core as codegen
from kiwicgen_logging import AsyncLogDispatcher
from kiwicgen_version import __version__


# =====================================================================================================================
# CLI presentation and argument parsing
# =====================================================================================================================

class KiwicgenHelpFormatter(argparse.RawDescriptionHelpFormatter):
    """Compact, stable help layout for the standalone kiwicgen CLI."""

    def __init__(self, prog: str) -> None:
        super().__init__(prog, max_help_position=36, width=120)


def _build_parser() -> argparse.ArgumentParser:
    """Construct the stable command-line interface exposed by kiwicgen."""
    parser = argparse.ArgumentParser(
        prog="kiwicgen",
        usage="%(prog)s [options]",
        description="KIWI component-scoped OSAL code generator (kiwicgen).",
        epilog=(
            "Examples:\n"
            "  kiwicgen --module-prefix=foo_module --port=FreeRTOS --use-thread-api\n"
            "  kiwicgen --fprof=kiwicgen-foo_module-profile.yaml\n"
            "  kiwicgen --help"
        ),
        formatter_class=KiwicgenHelpFormatter,
        add_help=False,
    )

    general = parser.add_argument_group("General options")
    general.add_argument(
        "-h",
        "--help",
        action="help",
        help="Show this help message and exit.",
    )
    general.add_argument(
        "--version",
        action="version",
        version=f"%(prog)s {__version__}",
        help="Show kiwicgen version and exit.",
    )
    general.add_argument(
        "--quiet",
        action="store_true",
        help="Suppress informational generation log messages.",
    )
    general.add_argument(
        "--no-color",
        action="store_true",
        help="Disable colored console log output.",
    )
    general.add_argument(
        "--module-prefix",
        metavar="PREFIX",
        help="Module prefix used for generated files and symbols.",
    )
    general.add_argument(
        "--port",
        metavar="PORT",
        help="Target OS port, e.g. FreeRTOS.",
    )
    general.add_argument(
        "--output",
        metavar="DIR",
        help="Output root directory. Default: ./generated relative to the current working directory.",
    )
    general.add_argument(
        "--fprof",
        metavar="PROFILE.yaml",
        help="Load code-generation parameters from a kiwicgen YAML profile.",
    )

    api_group = parser.add_argument_group("API selection")
    for api_name in codegen.SUPPORTED_APIS:
        option_name = api_name.replace("_", "-")
        api_group.add_argument(
            f"--use-{option_name}-api",
            action="store_true",
            default=None,
            help=f"Enable the {api_name.replace('_', ' ')} API group.",
        )

    layout = parser.add_argument_group("Output layout")
    layout.add_argument(
        "--split-into-port-dir",
        action="store_true",
        default=None,
        help="Place portable implementation into portable/<port>/.",
    )
    layout.add_argument(
        "--split-src-inc-files",
        action="store_true",
        default=None,
        help="Split generated .c and .h files into src/ and include/ directories.",
    )
    formatting = layout.add_mutually_exclusive_group()
    formatting.add_argument(
        "--format-generated-code",
        dest="format_generated_code",
        action="store_true",
        default=None,
        help="Format generated .c and .h files (default).",
    )
    formatting.add_argument(
        "--no-format-generated-code",
        dest="format_generated_code",
        action="store_false",
        help="Leave generated .c and .h files exactly as rendered from templates.",
    )
    return parser


def _console_log(message: str, *, use_color: bool) -> None:
    """Print one structured kiwicgen status line with optional console color."""
    if not use_color:
        print(message)
        return

    color = Fore.WHITE
    if message.startswith("[STEP]"):
        color = Fore.CYAN
    elif message.startswith("[ OK ]"):
        color = Fore.GREEN
    elif message.startswith("[WARN]"):
        color = Fore.YELLOW
    elif message.startswith("[ERR ]"):
        color = Fore.RED

    print(f"{color}{message}{Style.RESET_ALL}")


# =====================================================================================================================
# Effective configuration resolution
# =====================================================================================================================

def _config_from_args(args: argparse.Namespace) -> codegen.GenerationConfig:
    """Apply defaults < YAML profile < explicit CLI arguments precedence."""
    base = (
        codegen.load_profile(args.fprof)
        if args.fprof
        else codegen.make_generation_config()
    )

    module_prefix = (
        args.module_prefix if args.module_prefix is not None else base.module_prefix
    )
    port = args.port if args.port is not None else base.port
    selected = set(base.apis)

    for api_name in codegen.SUPPORTED_APIS:
        value = getattr(args, f"use_{api_name}_api")
        if value is True:
            selected.add(api_name)

    split_into_port_dir = (
        args.split_into_port_dir
        if args.split_into_port_dir is not None
        else base.split_into_port_dir
    )
    split_src_inc_files = (
        args.split_src_inc_files
        if args.split_src_inc_files is not None
        else base.split_src_inc_files
    )
    format_generated_code = (
        args.format_generated_code
        if args.format_generated_code is not None
        else base.format_generated_code
    )

    return codegen.make_generation_config(
        module_prefix=module_prefix,
        port=port,
        apis=selected,
        split_into_port_dir=split_into_port_dir,
        split_src_inc_files=split_src_inc_files,
        format_generated_code=format_generated_code,
    )


# =====================================================================================================================
# Application entry point
# =====================================================================================================================

def main(argv: list[str] | None = None) -> int:
    """Standalone console frontend for the shared kiwicgen core."""
    # Milestone 1: build the frontend-only command surface. Generation rules
    # remain entirely inside kiwicgen_core.
    parser = _build_parser()
    effective_argv = list(sys.argv[1:] if argv is None else argv)

    if not effective_argv:
        parser.print_help()
        return 0

    args = parser.parse_args(effective_argv)
    use_color = not args.no_color and sys.stdout.isatty() and not os.getenv("NO_COLOR")
    if use_color:
        just_fix_windows_console()

    dispatcher = None
    log_callback = None
    if not args.quiet:
        dispatcher = AsyncLogDispatcher(
            lambda message: _console_log(message, use_color=use_color)
        )
        log_callback = dispatcher.log

    try:
        # Milestone 2: collapse defaults, an optional YAML profile and explicit
        # CLI overrides into one normalized shared-core configuration.
        config = _config_from_args(args)
        output_root = (
            pathlib.Path(args.output)
            if args.output
            else pathlib.Path.cwd() / "generated"
        )

        # Milestone 3: delegate the complete render/write/format pipeline to
        # the shared core. The CLI does not post-process generated artifacts.
        codegen.generate(config, output_root, log_callback=log_callback)
    except (codegen.CodegenError, OSError) as exc:
        if dispatcher is not None:
            dispatcher.flush()

        message = f"[ERR ] {exc}"
        if use_color:
            print(f"{Fore.RED}{message}{Style.RESET_ALL}", file=sys.stderr)
        else:
            print(message, file=sys.stderr)
        return 2
    finally:
        if dispatcher is not None:
            dispatcher.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
