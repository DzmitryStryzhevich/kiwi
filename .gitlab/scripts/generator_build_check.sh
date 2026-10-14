#!/usr/bin/env bash

# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Kiwi contributors

set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

ci_install_build_tools

python -m pip install --upgrade pip build
python -m compileall -q generator/core generator/formatter generator/cli generator/gui
python -m build generator --outdir dist-generator
python -m pip install dist-generator/*.whl

kiwicgen --version
kiwicgen --help
