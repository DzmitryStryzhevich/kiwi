# KIWI Code Generator — `kiwicgen`

The `generator/` directory contains the shared KIWI code-generation core and two frontends: a standalone command-line application and a graphical application.

The architectural rule is simple: **generation logic belongs in `kiwicgen_core.py`; CLI and GUI are frontends only.** This prevents the two applications from developing separate generation behavior as KIWI grows.

```text
kiwicgen_cli.py ----+
                      |
                      v
               kiwicgen_core.py
                      ^
                      |
kiwicgen_gui.py ------+
```

## Files

```text
generator/
├── kiwicgen_core.py             Shared generation core
├── kiwicgen_cli.py              CLI frontend
├── kiwicgen_gui.py              GUI frontend
├── kiwicgen_version.py          Single source of the tool version
├── kiwicgen-clang-format.yaml   Fixed generated-C formatting policy
├── pyproject.toml               Python/runtime/build dependency metadata
├── scripts/
│   ├── kiwicgen-build-exe.bat   Windows CMD build helper
│   ├── kiwicgen-build-exe.ps1   Windows PowerShell build helper
│   └── kiwicgen-build-exe.sh    Linux/macOS build helper
├── build/                       Default intermediate build directory
└── dist/                        Default standalone distribution directory
```

## Requirements

Python 3.10 or newer is required to run kiwicgen from source or to build standalone executables. Runtime and executable-build dependencies are declared in `pyproject.toml`.

For source execution, install the generator project from `generator/`:

```bash
python -m pip install .
```

`kiwicgen` owns the formatting of generated C sources. The supported formatter baseline is `clang-format` 22.x, declared in `pyproject.toml`, while the formatting policy itself lives in `kiwicgen-clang-format.yaml`. The shared core passes that policy explicitly after rendering, so generated `.c` and `.h` files do not depend on a user or parent-project `.clang-format` configuration.

Standalone distributions carry their own `clang-format` executable, formatter policy and OSAL templates; no separate Python or formatter installation is required on the target machine.

## Running from source

CLI:

```bash
python generator/kiwicgen_cli.py --help
```

GUI:

```bash
python generator/kiwicgen_gui.py
```

## CLI application

The packaged CLI executable is named `kiwicgen` (`kiwicgen.exe` on Windows).

Running it with no arguments does **not** generate anything. It prints the same help text as `--help`:

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
| `--port=PORT` | Select target backend, currently `FreeRTOS` is implemented |
| `--output=DIR` | Output root directory |
| `--fprof=PROFILE.yaml` | Load a kiwicgen generation profile from YAML |

If `--output` is omitted, the CLI writes to `./generated` relative to the **current working directory**. The output directory is a runtime destination and is intentionally not stored in the generation profile.

### API selection

The current CLI supports the following positive switches:

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

All API groups are disabled by default. There are currently no `--no-*` switches: if an API switch is not provided, it is not enabled by that CLI argument.

When a profile is loaded with `--fprof`, positive `--use-*-api` switches can enable additional groups. Boolean groups already enabled by a profile are not disabled from the CLI because negative switches are intentionally not implemented at this stage.

### Output-layout switches

| Option | Meaning |
| --- | --- |
| `--split-into-port-dir` | Put portable source files below `portable/<port>/` |
| `--split-src-inc-files` | Split `.c` files into `src/` and `.h` files into `include/` |

Both are disabled by default.

### CLI examples

Generate a FreeRTOS OSAL with thread and queue APIs:

```bash
kiwicgen \
  --module-prefix=foo_module \
  --port=FreeRTOS \
  --use-thread-api \
  --use-queue-api
```

Generate into a custom directory:

```bash
kiwicgen \
  --module-prefix=foo_module \
  --port=FreeRTOS \
  --use-thread-api \
  --output=./out
```

Regenerate from a saved profile:

```bash
kiwicgen --fprof=kiwicgen-foo_module-profile.yaml
```

Regenerate from a profile into another directory:

```bash
kiwicgen \
  --fprof=kiwicgen-foo_module-profile.yaml \
  --output=./regen
```

## YAML generation profiles

A generation profile captures **what kiwicgen should generate**, not where the result should be written. It can therefore be committed beside a component and reused later with a newer generator version.

A profile currently has this shape:

```yaml
kiwicgen-profile-version: 3
module_prefix: foo_module
port: FreeRTOS
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
```

`kiwicgen-profile-version` is the version of the profile schema. Version 3 introduces the `kiwicgen-` naming for newly saved profiles. Legacy version 1 and 2 profiles that use `kiwi_profile_version` remain accepted on load; the transitional `kiwicgen_profile_version` spelling is also accepted on load. Newly saved profiles always use the version 3 key shown above. The profile schema version is independent of the OSAL implementation version.

The default file name suggested by the GUI is:

```text
kiwicgen-<module_prefix>-profile.yaml
```

For example:

```text
kiwicgen-foo_module-profile.yaml
```

### Configuration precedence in the CLI

The CLI builds the effective configuration in this order:

```text
built-in defaults
       ↓
YAML profile (--fprof)
       ↓
explicit CLI scalar/positive options
```

`--module-prefix` and `--port` can replace values loaded from a profile. Positive API/layout switches can enable options in addition to those already enabled by the profile.

## GUI application

The packaged GUI executable is named `kiwicgen-gui` (`kiwicgen-gui.exe` on Windows). The GUI imports `kiwicgen_core.py` directly; it does not maintain a second generator implementation and does not shell out to the CLI executable.

### Paths settings

The upper settings group controls the target and generated-directory layout.

| Control | Meaning |
| --- | --- |
| `Port` | Select target backend. `FreeRTOS` is currently implemented; other entries are reserved for planned ports |
| `Module Prefix` | Prefix used to derive generated file names and C symbol naming forms |
| `Output Folder` | Root folder in which the module directory is generated |
| `Browse` | Select the output folder using the system directory picker |
| `Split into port directory` | Same behavior as CLI `--split-into-port-dir` |
| `Split source/include files` | Same behavior as CLI `--split-src-inc-files` |

`FreeRTOS` appears first in the port selector and is the current default port.

### API Set checkboxes

The API selector contains the currently implemented FreeRTOS primitive groups:

| Checkbox | Effect |
| --- | --- |
| `Queues` | Include queue lifecycle plus `Put`, `Post`, `Get`, `Wait`, `Pend` and reset operations |
| `Stream Buffers` | Include byte-stream create/delete/send/receive/reset operations |
| `Locks` | Include recursive mutual-exclusion lock objects |
| `Counting Semaphores` | Include create/delete/acquire/acquire-with-timeout/release/count operations |
| `Threads` | Include thread lifecycle, suspend/resume, delay and self-exit operations |
| `Critical Sections` | Include enter/exit operations for short FreeRTOS critical regions |
| `Software Timers` | Include create/delete/start/stop/reset, one-shot/auto-reload and callback configuration |
| `Time` | Include system time retrieval |
| `Memory` | Include OS-backed allocation/free with OSAL registry bookkeeping |

These correspond directly to the CLI `--use-*-api` switches and to fields in the YAML profile.

`Event Flags / Groups (planned)` remains visible only as a disabled roadmap item. It does not affect generation.

### GUI buttons

| Button | Behavior |
| --- | --- |
| `Generate` | Validate current UI settings and invoke the shared generation core |
| `Load Profile...` | Read and validate a YAML profile through `kiwicgen_core.load_profile()`, then populate the GUI controls |
| `Save Profile...` | Normalize current UI settings and save them through `kiwicgen_core.save_profile()` |
| `Open Output Folder` | Open the currently configured output directory in the platform file manager |

### Load Profile

`Load Profile...` is intended for repeatable regeneration. A typical workflow is:

```text
existing component
      |
      v
kiwicgen-foo_module-profile.yaml
      |
      v
new KIWI version
      |
      v
Load Profile...
      |
      v
review or modify settings
      |
      v
Generate
```

Loading a profile updates the module prefix, port, implemented API checkboxes and layout checkboxes. The output folder remains a local runtime choice and is not read from the YAML profile.

### Save Profile

`Save Profile...` can be used independently of generation. The GUI validates and normalizes the current settings, then proposes:

```text
kiwicgen-<module_prefix>-profile.yaml
```

The saved file can later be passed directly to the CLI with `--fprof` or loaded back into the GUI.

### Generator log

The lower `Generator log` area receives messages from the shared core during generation and profile operations. Generated file paths and errors are shown there so the GUI uses the same generation feedback path as the CLI backend logic. CLI, GUI and build scripts use the same status vocabulary: `[INFO]`, `[STEP]`, `[ OK ]`, `[WARN]` and `[ERR ]`.

Generation is still synchronous in the current GUI implementation. Moving generation to a worker thread and making the UI fully responsive during long operations is intentionally outside the build/distribution iteration.

## Output layouts

Assume the module prefix is `foo_module`.

### Default layout

With both layout checkboxes/switches disabled:

```text
foo_module/
├── foo_module_osal.c
├── foo_module_osal.h
├── foo_module_osal_freertos.c
├── foo_module_osal_freertos.h
├── foo_module_osal_profile.h
└── CMakeLists.txt
```

### Split into port directory

With only `Split into port directory` enabled:

```text
foo_module/
├── foo_module_osal.c
├── foo_module_osal.h
├── foo_module_osal_profile.h
├── CMakeLists.txt
└── portable/
    └── freertos/
        ├── foo_module_osal_freertos.c
        ├── foo_module_osal_freertos.h
        └── CMakeLists.txt
```

### Split source/include files

With only `Split source/include files` enabled:

```text
foo_module/
├── include/
│   ├── foo_module_osal.h
│   ├── foo_module_osal_freertos.h
│   └── foo_module_osal_profile.h
├── src/
│   ├── foo_module_osal.c
│   └── foo_module_osal_freertos.c
└── CMakeLists.txt
```

### Both layout options enabled

```text
foo_module/
├── include/
│   ├── foo_module_osal.h
│   └── foo_module_osal_profile.h
├── src/
│   └── foo_module_osal.c
├── CMakeLists.txt
└── portable/
    └── freertos/
        ├── include/
        │   └── foo_module_osal_freertos.h
        ├── src/
        │   └── foo_module_osal_freertos.c
        └── CMakeLists.txt
```

## Building standalone executables

The standalone build follows the same isolated build model on Windows and Linux/macOS. Build scripts live in `generator/scripts/`; by default, both output directories are created one level above that directory:

```text
generator/
├── scripts/
├── build/
└── dist/
```

Windows CMD:

```cmd
generator\scripts\kiwicgen-build-exe.bat
```

Windows PowerShell:

```powershell
.\generator\scripts\kiwicgen-build-exe.ps1
```

Linux/macOS:

```sh
./generator/scripts/kiwicgen-build-exe.sh
```

The build performs these stages:

```text
search/select Python runtime
        ↓
create or reuse build/venv
        ↓
install .[executable] dependencies
        ↓
read kiwicgen_version.py
        ↓
build kiwicgen and kiwicgen-gui
        ↓
stage templates, formatter and GUI resources
        ↓
verify the final distribution
```

Python can be selected explicitly for CI/non-interactive builds:

```text
--python <python executable>
```

The default output paths can also be overridden:

```text
--build-dir <path>
--dist-dir <path>
```

The default standalone distribution is self-contained and has this shape on Windows:

```text
generator/dist/
├── kiwicgen.exe
├── kiwicgen-gui.exe
├── kiwicgen-clang-format.yaml
├── README.md
├── kiwicgen-README.md
├── LICENSE
├── tools/
│   └── clang-format.exe
├── osal/
│   └── ... templates and CMake files ...
└── doc/
    └── ... GUI artwork ...
```

Linux/macOS use the same layout without the `.exe` suffix. The distribution does not depend on the source repository or current working directory. Runtime resources are resolved relative to the executable directory.

Build logs use the common status vocabulary:

```text
[INFO] informational state
[STEP] current build stage
[ OK ] successful stage
[WARN] recoverable problem
[ERR ] fatal error
```

The final smoke check runs `kiwicgen --version` from the completed `dist/` directory and verifies that required external templates and formatter resources were staged.

### Version

The single source of the kiwicgen version is:

```text
generator/kiwicgen_version.py
```

The same version is used by `kiwicgen --version`, Python project metadata and Windows PE version resources for both executables.

## Extending the generator

New generation behavior should first be implemented in `kiwicgen_core.py`. CLI and GUI should expose that behavior without duplicating template-processing, validation, profile or output-layout logic.

When adding a new option, consider all three interfaces together:

1. normalized representation in `GenerationConfig`;
2. YAML profile serialization/deserialization;
3. CLI switch and/or GUI control.

This keeps saved profiles, CLI use and GUI generation equivalent.
