# KIWI Code Generator — `kiwicgen`

The `generator/` directory contains the KIWI OSAL code generator, its CLI and GUI frontends, the generated-source formatter integration, and the runtime resources required by standalone builds.

Both frontends use the same generation core. Generation behavior must not be duplicated in the GUI or CLI.

```text
CLI ----+
        |
        v
      core ----> formatter
        ^
        |
GUI ----+

core ----> resources/templates/osal
```

## Source layout

```text
generator/
├── core/
│   ├── __init__.py
│   ├── model.py
│   ├── profile.py
│   ├── paths.py
│   ├── validation.py
│   ├── template_renderer.py
│   ├── generator.py
│   ├── logging.py
│   ├── version.py
│   └── errors.py
├── formatter/
│   ├── __init__.py
│   ├── formatter.py
│   └── resolver.py
├── cli/
│   ├── __init__.py
│   └── main.py
├── gui/
│   ├── __init__.py
│   └── main.py
├── resources/
│   ├── templates/
│   │   └── osal/
│   └── kiwicgen-clang-format.yaml
├── scripts/
│   ├── build-exe.bat
│   ├── build-exe.ps1
│   └── build-exe.sh
├── pyproject.toml
└── README.md
```

Responsibilities are intentionally separated:

- `core/model.py` contains frontend-independent configuration/result data and stable capability lists.
- `core/profile.py` loads/saves YAML profiles and validates `kiwicgen-version` constraints.
- `core/paths.py` resolves source/distribution resources and generated output paths.
- `core/validation.py` normalizes and validates configuration shared by CLI, GUI and profiles.
- `core/template_renderer.py` performs OSAL template text transformations and generated CMake/profile rendering.
- `core/generator.py` orchestrates the generation pipeline.
- `core/logging.py` provides the asynchronous log dispatcher shared by frontends.
- `core/version.py` is the single source of the application version.
- `core/errors.py` defines expected kiwicgen error classes.
- `formatter/` owns clang-format resolution and execution.
- `cli/` and `gui/` contain frontend-only behavior.
- `resources/templates/osal/` contains the current OSAL templates.

## Requirements

Python 3.10 or newer is required to run kiwicgen from source or build standalone executables. Runtime and executable-build dependencies are declared in `pyproject.toml`.

From `generator/`, install source dependencies with:

```bash
python -m pip install .
```

`kiwicgen` owns the formatting of generated C sources. The supported formatter baseline is `clang-format` 22.x. The fixed style policy is stored in:

```text
generator/resources/kiwicgen-clang-format.yaml
```

Standalone distributions carry their own formatter executable, style policy and OSAL templates, so target machines do not need a separate Python or clang-format installation.

## Running from source

From the repository root:

```bash
python generator/cli/main.py --help
python generator/gui/main.py
```

The installed console entry point is also available after installing the project:

```bash
kiwicgen --help
```

## CLI application

The packaged CLI executable is named `kiwicgen` (`kiwicgen.exe` on Windows).

Running it with no arguments prints help and does not generate files:

```bash
kiwicgen
kiwicgen --help
```

### General options

| Option | Purpose |
| --- | --- |
| `-h`, `--help` | Print CLI help and exit |
| `--version` | Print the kiwicgen version and exit |
| `--quiet` | Suppress informational generation log messages |
| `--no-color` | Disable colored console log output |
| `--module-prefix=PREFIX` | Prefix used for generated file names, symbols and include guards |
| `--port=PORT` | Select a target backend; repeat to select multiple ports |
| `--language=LANGUAGE` | Select generated language; C is implemented and C++ is planned |
| `--output=DIR` | Output root directory |
| `--fprof=PROFILE.yaml` | Load a kiwicgen generation profile from YAML |

If `--output` is omitted, the CLI writes to `./generated` relative to the current working directory. Output location is intentionally not stored in profiles.

### API selection

The current CLI supports these positive switches:

| Option | API group |
| --- | --- |
| `--use-queue-api` | Queues |
| `--use-stream-buffer-api` | Stream buffers |
| `--use-lock-api` | Locks |
| `--use-semaphore-api` | Counting semaphores |
| `--use-thread-api` | Threads |
| `--use-critical-section-api` | Critical sections |
| `--use-software-timer-api` | Software timers |
| `--use-time-api` | Time |
| `--use-memory-api` | Memory |

All API groups are disabled by default. Positive CLI switches add groups to those loaded from a profile.

### Output-layout switches

| Option | Meaning |
| --- | --- |
| `--split-into-port-dir` | Put portable files below `portable/<port>/` |
| `--split-src-inc-files` | Split `.c` files into `src/` and `.h` files into `include/` |
| `--format-generated-code` | Format generated `.c`/`.h` files with the kiwicgen policy |
| `--no-format-generated-code` | Skip generated-code formatting |

Generated-code formatting is enabled by default.

### CLI examples

Generate a FreeRTOS OSAL with thread and queue APIs:

```bash
kiwicgen \
  --module-prefix=foo_module \
  --port=FreeRTOS \
  --language=C \
  --use-thread-api \
  --use-queue-api
```

Regenerate from a saved profile into another directory:

```bash
kiwicgen \
  --fprof=kiwicgen-foo_module-profile.yaml \
  --output=./regen
```

## YAML generation profiles

A generation profile captures what kiwicgen should generate, not where output is written.

Profiles saved by kiwicgen 0.3.x use this shape:

```yaml
kiwicgen-version: ">=0.3.0,<1.0.0"
module_prefix: foo_module
ports:
  - FreeRTOS
language: C
api:
  queue: true
  stream_buffer: false
  lock: false
  semaphore: false
  thread: true
  critical_section: false
  software_timer: true
  time: false
  memory: false
layout:
  split_into_port_dir: true
  split_src_inc_files: true
format_generated_code: true
```

`kiwicgen-version` is a standard version constraint. The running generator validates it before consuming generation options. Existing compatible 0.x profiles, for example `>=0.2.0,<1.0.0`, remain valid with kiwicgen 0.3.0.

Starting with kiwicgen 1.0.0, profile evolution is intended to preserve backward compatibility within a major release.

The GUI suggests profile names in this form:

```text
kiwicgen-<module_prefix>-profile.yaml
```

### CLI precedence

```text
built-in defaults
       ↓
YAML profile (--fprof)
       ↓
explicit CLI scalar/positive options
```

`--module-prefix`, repeated `--port` and `--language` replace corresponding profile values. Positive API/layout switches can enable additional options. Formatting switches explicitly enable or disable formatting.

## GUI application

The packaged GUI executable is named `kiwicgen-gui` (`kiwicgen-gui.exe` on Windows).

The GUI uses three execution contexts:

```text
Tk/UI main thread
Generation worker thread
Logging listener thread
```

Tk widgets are touched only by the UI thread. Generation and formatter work run outside it, and logs are serialized through thread-safe queues before being displayed by Tk.

The settings area remains compact when the window is resized; extra space is assigned primarily to the generator log.

### Current selectors

Ports:

- FreeRTOS — implemented.
- POSIX — planned, selectable in the GUI, but generation is not implemented yet.

Languages:

- C — implemented.
- C++ — planned, selectable in the GUI, but generation is not implemented yet.

`Event Flags / Groups (planned)` is also selectable for UI planning purposes, but it is not part of the generated API yet.

Before starting a generation worker, the GUI validates configuration. At least one port and a language must be selected. Selecting an unimplemented port/language produces both a generator-log error and a GUI error message; generation is not started.

## Generated resources and formatter

Source execution resolves resources from:

```text
generator/resources/templates/osal/
generator/resources/kiwicgen-clang-format.yaml
```

Standalone execution resolves external resources relative to the executable directory:

```text
dist/
├── kiwicgen[.exe]
├── kiwicgen-gui[.exe]
├── templates/
│   └── osal/
├── kiwicgen-clang-format.yaml
├── tools/
│   └── clang-format[.exe]
├── doc/
├── README.md
├── kiwicgen-README.md
└── LICENSE
```

## Building standalone executables

Build helpers live in `generator/scripts/` and resolve their default build/dist directories relative to their own location.

Windows CMD:

```bat
generator\scripts\build-exe.bat
```

Windows PowerShell:

```powershell
generator\scripts\build-exe.ps1
```

Linux/macOS:

```bash
generator/scripts/build-exe.sh
```

Optional arguments:

```text
--python <path>
--build-dir <path>
--dist-dir <path>
```

The build uses an isolated `build/venv`, stages packaging input under `build/`, installs build dependencies there, builds both frontends with PyInstaller and then stages external runtime resources.

The build scripts finish with a distribution smoke test: they run `kiwicgen --version`, perform a real FreeRTOS OSAL generation using the final executable and verify that the expected generated header exists.

## Development checks

At minimum, before handing off a generator change:

```bash
python -m compileall -q generator/core generator/formatter generator/cli generator/gui
python generator/cli/main.py --help
python generator/cli/main.py --version
```

A functional smoke check should also load a profile or run a representative FreeRTOS generation. Changes affecting distribution/build behavior should run the platform build helper when the current environment supports it.
