# KIWI Code Generator

The `generator/` directory contains the shared KIWI code-generation core and two frontends: a standalone command-line application and a graphical application.

The architectural rule is simple: **generation logic belongs in `kiwi_codegen.py`; CLI and GUI are frontends only.** This prevents the two applications from developing separate generation behavior as KIWI grows.

```text
kiwi_codegen_cli_app.py ----+
                            |
                            v
                     kiwi_codegen.py
                            ^
                            |
kiwi_codegen_ui_app.py -----+
```

## Files

```text
generator/
├── kiwi_codegen.py           Shared generation core
├── kiwi_codegen_cli_app.py   CLI frontend
├── kiwi_codegen_ui_app.py    GUI frontend
├── kiwi.spec                 PyInstaller spec for CLI executable
├── kiwi_gui.spec             PyInstaller spec for GUI executable
├── build_exe.bat             Windows build helper
└── requirements.txt          Python dependencies
```

## Requirements

Install the Python dependencies from the repository root or from `generator/`:

```bash
python -m pip install -r generator/requirements.txt
```

## Running from source

CLI:

```bash
python generator/kiwi_codegen_cli_app.py --help
```

GUI:

```bash
python generator/kiwi_codegen_ui_app.py
```

## CLI application

The packaged CLI executable is named `kiwi` (`kiwi.exe` on Windows).

Running it with no arguments does **not** generate anything. It prints the same help text as `--help`:

```bash
kiwi
kiwi --help
```

### General options

| Option | Purpose |
| --- | --- |
| `-h`, `--help` | Print CLI help and exit |
| `--module-prefix=PREFIX` | Prefix used for generated file names, symbols and include guards |
| `--port=PORT` | Select target backend, currently `FreeRTOS` is implemented |
| `--output=DIR` | Output root directory |
| `--fprof=PROFILE.yaml` | Load a KIWI code-generation profile from YAML |

If `--output` is omitted, the CLI writes to `./generated` relative to the **current working directory**. The output directory is a runtime destination and is intentionally not stored in the generation profile.

### API selection

The current CLI supports the following positive switches:

| Option | API group |
| --- | --- |
| `--use-queue-api` | Queues |
| `--use-lock-api` | Locks |
| `--use-thread-api` | Threads |
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
kiwi \
  --module-prefix=foo_module \
  --port=FreeRTOS \
  --use-thread-api \
  --use-queue-api
```

Generate into a custom directory:

```bash
kiwi \
  --module-prefix=foo_module \
  --port=FreeRTOS \
  --use-thread-api \
  --output=./out
```

Regenerate from a saved profile:

```bash
kiwi --fprof=foo_module_kiwi_profile.yaml
```

Regenerate from a profile into another directory:

```bash
kiwi \
  --fprof=foo_module_kiwi_profile.yaml \
  --output=./regen
```

## YAML generation profiles

A generation profile captures **what KIWI should generate**, not where the result should be written. It can therefore be committed beside a component and reused later with a newer generator version.

A profile currently has this shape:

```yaml
kiwi_profile_version: 1
module_prefix: foo_module
port: FreeRTOS
api:
  queue: true
  lock: false
  thread: true
  time: false
  memory: false
layout:
  split_into_port_dir: true
  split_src_inc_files: true
```

`kiwi_profile_version` is the version of the profile schema. It is independent of the OSAL implementation version and exists so future versions of KIWI can validate or migrate older profile formats deliberately.

The default file name suggested by the GUI is:

```text
<module_prefix>_kiwi_profile.yaml
```

For example:

```text
foo_module_kiwi_profile.yaml
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

The packaged GUI executable is named `kiwi_gui` (`kiwi_gui.exe` on Windows). The GUI uses `kiwi_codegen.py` directly; it does not maintain a second generator implementation and does not shell out to the CLI executable.

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

The first row contains the currently implemented generator selections:

| Checkbox | Effect |
| --- | --- |
| `Queues` | Include the queue API group |
| `Locks` | Include the lock API group |
| `Threads` | Include the thread API group |
| `Time` | Include the time API group |
| `Memory` | Include the memory API group |

These correspond directly to the CLI `--use-*-api` switches.

The GUI also currently displays roadmap placeholders for:

- `Event Groups`;
- `Software Timers`;
- `Stream Buffers`.

These three controls are **not connected to generation yet** and do not change generated output. They are intentionally left as placeholders until those primitive groups are implemented in the OSAL templates and shared code-generation core.

### GUI buttons

| Button | Behavior |
| --- | --- |
| `Generate` | Validate current UI settings and invoke the shared generation core |
| `Load Profile...` | Read and validate a YAML profile through `kiwi_codegen.load_profile()`, then populate the GUI controls |
| `Save Profile...` | Normalize current UI settings and save them through `kiwi_codegen.save_profile()` |
| `Open Output Folder` | Open the currently configured output directory in the platform file manager |

### Load Profile

`Load Profile...` is intended for repeatable regeneration. A typical workflow is:

```text
existing component
      |
      v
foo_module_kiwi_profile.yaml
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
<module_prefix>_kiwi_profile.yaml
```

The saved file can later be passed directly to the CLI with `--fprof` or loaded back into the GUI.

### Generator log

The lower `Generator log` area receives messages from the shared core during generation and profile operations. Generated file paths and errors are shown there so the GUI uses the same generation feedback path as the CLI backend logic.

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

## Building Windows executables

Run:

```text
generator\build_exe.bat
```

The script installs the required Python dependencies and invokes PyInstaller for both frontends.

Expected output:

```text
generator\dist\kiwi.exe      CLI application
generator\dist\kiwi_gui.exe  GUI application
```

The CLI executable is built with a console. The GUI executable is built without a console window.

Both bundles include the OSAL template tree required for standalone generation. The GUI bundle also uses the KIWI artwork stored in `doc/`.

## Extending the generator

New generation behavior should first be implemented in `kiwi_codegen.py`. CLI and GUI should expose that behavior without duplicating template-processing, validation, profile or output-layout logic.

When adding a new option, consider all three interfaces together:

1. normalized representation in `GenerationConfig`;
2. YAML profile serialization/deserialization;
3. CLI switch and/or GUI control.

This keeps saved profiles, CLI use and GUI generation equivalent.
