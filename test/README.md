# KIWI Testing

Automated tests are not implemented yet. The `test/` directory is reserved for the project test infrastructure and is expected to grow together with the generator and portable backends.

## Planned coverage

The test strategy should eventually include:

- code-generation core unit tests;
- configuration normalization and validation tests;
- YAML profile load/save round-trip tests;
- deterministic/golden-file generation tests;
- CLI argument and exit-code tests;
- output-layout tests for every supported combination;
- generated C syntax/build checks;
- backend-specific tests for FreeRTOS, POSIX and CMSIS-RTOS2;
- resource lifecycle/registry tests;
- assertion, tracing and error-path tests;
- regression tests for reported bugs.

GUI logic should be kept thin enough that most behavior can be tested through the shared `kiwi_codegen.py` core. GUI-specific tests can then focus on frontend state mapping and profile load/save interaction rather than duplicating generator tests.

## Current manual smoke test

Until the automated suite exists, a basic development check can be performed from the repository root.

Check Python syntax:

```bash
python -m py_compile \
  generator/kiwi_codegen.py \
  generator/kiwi_codegen_cli_app.py \
  generator/kiwi_codegen_ui_app.py
```

Check CLI help/no-argument behavior:

```bash
python generator/kiwi_codegen_cli_app.py
python generator/kiwi_codegen_cli_app.py --help
```

Neither command should generate files.

Perform a sample generation into a temporary output directory:

```bash
python generator/kiwi_codegen_cli_app.py \
  --module-prefix=foo_module \
  --port=FreeRTOS \
  --use-thread-api \
  --use-queue-api \
  --output=./generated_smoke
```

Then verify that the expected module tree is produced and that disabled API groups are not present in generated sources.

Profile round-trip and deterministic output checks will be converted into automated tests as the test infrastructure is introduced.
