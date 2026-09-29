#!/usr/bin/env bash
set -euo pipefail

export LD_LIBRARY_PATH="$SALTS_ROOT/lib:$SALTS_UTILS_ROOT/lib:$CHTTP_ROOT/lib:$GITHUB_WORKSPACE/vcpkg_installed/x64-linux/lib:${LD_LIBRARY_PATH:-}"

# Keep LSan strict for TurboSCXML-owned tests. The W3C consumer case currently
# exposes a retained SDS allocation in published Salts.Native, tracked by
# qigao/salts#630. ASan and UBSan remain enabled for that case.
ctest --test-dir build/ci-sanitizers --no-tests=error --output-on-failure \
  -E '^scxml_w3c_conformance_test$'

ASAN_OPTIONS='detect_leaks=0:halt_on_error=1' \
  ctest --test-dir build/ci-sanitizers --no-tests=error --output-on-failure \
    -R '^scxml_w3c_conformance_test$'
