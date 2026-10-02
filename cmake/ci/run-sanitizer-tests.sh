#!/usr/bin/env bash
set -euo pipefail

export LD_LIBRARY_PATH="$SALTS_ROOT/lib:$SALTS_UTILS_ROOT/lib:$CHTTP_ROOT/lib:$GITHUB_WORKSPACE/vcpkg_installed/x64-linux/lib:${LD_LIBRARY_PATH:-}"

ctest --test-dir build/ci-sanitizers --no-tests=error --output-on-failure
