from __future__ import annotations

import argparse
import pathlib
import sys

import kiwi_codegen as codegen


class KiwiHelpFormatter(argparse.RawDescriptionHelpFormatter):
    """Compact, stable help layout for the standalone KIWI CLI."""

    def __init__(self, prog: str) -> None:
        super().__init__(prog, max_help_position=36, width=120)


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="kiwi",
        usage="%(prog)s [options]",
        description="KIWI component-scoped OSAL code generator.",
        epilog=(
            "Examples:\n"
            "  kiwi --module-prefix=foo_module --port=FreeRTOS --use-thread-api\n"
            "  kiwi --fprof=foo_module_kiwi_profile.yaml\n"
            "  kiwi --help"
        ),
        formatter_class=KiwiHelpFormatter,
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
        help="Load code-generation parameters from a KIWI YAML profile.",
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
    return parser


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

    return codegen.make_generation_config(
        module_prefix=module_prefix,
        port=port,
        apis=selected,
        split_into_port_dir=split_into_port_dir,
        split_src_inc_files=split_src_inc_files,
    )


def main(argv: list[str] | None = None) -> int:
    """Standalone console frontend for the KIWI code generator."""
    parser = _build_parser()
    effective_argv = list(sys.argv[1:] if argv is None else argv)

    if not effective_argv:
        parser.print_help()
        return 0

    args = parser.parse_args(effective_argv)

    try:
        config = _config_from_args(args)
        output_root = (
            pathlib.Path(args.output)
            if args.output
            else pathlib.Path.cwd() / "generated"
        )
        codegen.generate(config, output_root, log_callback=print)
    except (codegen.CodegenError, OSError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 2

    print("Done.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
