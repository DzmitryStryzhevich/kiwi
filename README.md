# KIWI OSAL Code Generator

KIWI generates C OSAL sources for embedded projects from configurable templates.

## Repository structure

```text
kiwi/
├── generator/                 Python GUI and generator build files
├── osal/                      Base OSAL template
│   ├── CMakeLists.txt
│   └── portable/
│       ├── freertos/          FreeRTOS port template
│       ├── posix/             POSIX port scaffold
│       └── cmsis_rtos2/       CMSIS-RTOS v2 port scaffold
├── test/                      Test infrastructure
├── doc/                       Images and project documentation
├── .github/                   GitHub CI/CD infrastructure
└── .gitlab/                   GitLab CI/CD infrastructure
```

## Supported ports

- `freertos` — supported
- `posix (coming soon)`
- `cmsis rtos v2 (coming soon)`

## Available API groups

- Queues
- Locks
- Threads
- Time
- Memory

## Run from sources

```bash
python generator/osal_codegen_app.py
```

## Build Windows executable

Run:

```text
generator/build_exe.bat
```

The executable is created in:

```text
generator/dist/OSAL_Code_Generator.exe
```

The PyInstaller package includes the `osal/` template tree, so the executable can resolve templates without the source repository beside it.
