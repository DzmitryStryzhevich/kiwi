#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Kiwi contributors

"""Integration checks for OSAL profile filtering and generated test build inputs."""

from __future__ import annotations

import argparse
import subprocess
import tempfile
from pathlib import Path


FULL_API_FLAGS = (
    "--use-queue-api",
    "--use-stream-buffer-api",
    "--use-mutex-api",
    "--use-semaphore-api",
    "--use-event-flags-api",
    "--use-thread-api",
    "--use-critical-section-api",
    "--use-software-timer-api",
    "--use-time-api",
    "--use-memory-api",
)


def run_generator(repo_root: Path, output_root: Path, prefix: str, flags: tuple[str, ...]) -> Path:
    command = [
        "python3",
        str(repo_root / "generator" / "cli" / "main.py"),
        f"--module-prefix={prefix}",
        "--port=FreeRTOS",
        "--port=POSIX",
        "--split-into-port-dir",
        "--no-format-generated-code",
        f"--output={output_root}",
        *flags,
    ]
    subprocess.run(command, cwd=repo_root, check=True)
    return output_root / prefix


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def verify_full_profile(repo_root: Path, temp_root: Path) -> None:
    generated = run_generator(repo_root, temp_root, "generator_full", FULL_API_FLAGS)

    generic_h = (generated / "generator_full_osal.h").read_text(encoding="utf-8")
    generic_c = (generated / "generator_full_osal.c").read_text(encoding="utf-8")
    posix_c = (generated / "portable" / "posix" / "generator_full_osal_posix.c").read_text(encoding="utf-8")
    freertos_c = (generated / "portable" / "freertos" / "generator_full_osal_freertos.c").read_text(encoding="utf-8")

    required_api = (
        "generatorFull_osalQueueCreate",
        "generatorFull_osalStreamBufferCreate",
        "generatorFull_osalSemaphoreCreate",
        "generatorFull_osalEventFlagsCreate",
        "generatorFull_osalThreadYield",
        "generatorFull_osalSoftwareTimerCreate",
    )
    for symbol in required_api:
        require(symbol in generic_h, f"full profile is missing {symbol}")

    require("sched_yield()" in posix_c, "POSIX full profile is missing native ThreadYield")
    require("timer_create(CLOCK_MONOTONIC" in posix_c, "POSIX full profile is missing native software timers")
    require("taskYIELD()" in freertos_c, "FreeRTOS full profile is missing ThreadYield")
    require("xTimerCreate" in freertos_c, "FreeRTOS full profile is missing software timers")

    for text in (generic_h, generic_c, posix_c, freertos_c):
        require("// BEGIN " not in text, "generated output still contains BEGIN markers")
        require("// END " not in text, "generated output still contains END markers")


def verify_thread_filtering(repo_root: Path, temp_root: Path) -> None:
    flags = tuple(flag for flag in FULL_API_FLAGS if flag != "--use-thread-api")
    generated = run_generator(repo_root, temp_root, "generator_no_thread", flags)

    generic_h = (generated / "generator_no_thread_osal.h").read_text(encoding="utf-8")
    posix_c = (generated / "portable" / "posix" / "generator_no_thread_osal_posix.c").read_text(encoding="utf-8")
    freertos_c = (generated / "portable" / "freertos" / "generator_no_thread_osal_freertos.c").read_text(encoding="utf-8")

    require("generatorNoThread_osalThreadYield" not in generic_h,
            "ThreadYield remained in the generic API when THREAD was disabled")
    require("sched_yield()" not in posix_c,
            "POSIX ThreadYield implementation remained when THREAD was disabled")
    require("taskYIELD()" not in freertos_c,
            "FreeRTOS ThreadYield implementation remained when THREAD was disabled")

    require("generatorNoThread_osalSoftwareTimerCreate" in generic_h,
            "disabling THREAD unexpectedly removed SOFTWARE_TIMER")
    require("generatorNoThread_osalEventFlagsCreate" in generic_h,
            "disabling THREAD unexpectedly removed EVENT_FLAGS")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    args = parser.parse_args()
    repo_root = args.repo_root.resolve()

    with tempfile.TemporaryDirectory(prefix="kiwi-generator-it-") as temporary_directory:
        temp_root = Path(temporary_directory)
        verify_full_profile(repo_root, temp_root / "full")
        verify_thread_filtering(repo_root, temp_root / "no-thread")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
