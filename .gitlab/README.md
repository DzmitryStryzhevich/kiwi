# GitLab CI layout

GitLab CI is split into orchestration fragments and executable shell helpers.
The repository-root `.gitlab-ci.yml` contains only local includes.

## CI fragments

| File | Purpose |
| --- | --- |
| `ci/common.yml` | Five-stage pipeline, shared variables, and hidden job templates. |
| `ci/generator.yml` | Generator build check and full OSAL generation jobs. |
| `ci/generated_code.yml` | Generated-code build/link check and static analysis jobs. |
| `ci/tests.yml` | Generator, generic unit, POSIX IT, and FreeRTOS-POSIX IT jobs. |

## Scripts

| Script | Purpose |
| --- | --- |
| `scripts/common.sh` | Shared environment, dependency installation, pinned dependency checkout, and CTest helpers. |
| `scripts/generator_build_check.sh` | Build/install the generator and verify CLI entry points. |
| `scripts/generate_full_osal_set.sh` | Generate the full FreeRTOS + POSIX OSAL set and validate the output. |
| `scripts/build_generated_code.sh` | Build/link generated code against the pinned FreeRTOS POSIX port. |
| `scripts/static_analysis.sh` | Run GCC `-fanalyzer` and cppcheck. |
| `scripts/test_generator.sh` | Run generator/profile integration tests. |
| `scripts/test_osal_unit.sh` | Run generic OSAL unit tests. |
| `scripts/test_osal_posix_it.sh` | Run native POSIX backend integration tests. |
| `scripts/test_osal_freertos_posix_it.sh` | Run FreeRTOS backend integration tests on `GCC_POSIX`. |

The YAML fragments describe job orchestration only.  Build, generation,
analysis, and test commands remain in the stage-specific shell scripts so the
pipeline is readable and the command sequences can be reviewed independently.
