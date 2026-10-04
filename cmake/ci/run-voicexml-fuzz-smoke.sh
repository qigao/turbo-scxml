#!/usr/bin/env bash
set -euo pipefail

build_dir="${GITHUB_WORKSPACE:-$PWD}/build/ci-sanitizers/tests"

required=(
  "$build_dir/voicexml_base_fuzz_smoke"
  "$build_dir/voicexml_cmeta_fuzz_smoke"
  "$build_dir/voicexml_resource_fuzz_smoke"
)

for target in "${required[@]}"; do
  if [ ! -x "$target" ]; then
    printf 'missing VoiceXML fuzz smoke target: %s\n' "$target" >&2
    exit 1
  fi
  printf 'running bounded fuzz smoke: %s\n' "$target"
  VOICEXML_FUZZ_TRACE=1 timeout --signal=KILL 30s "$target"
done

quickjs="$build_dir/voicexml_quickjs_fuzz_smoke"
if [ -x "$quickjs" ]; then
  printf 'running bounded fuzz smoke: %s\n' "$quickjs"
  VOICEXML_FUZZ_TRACE=1 timeout --signal=KILL 30s "$quickjs"
else
  printf 'QuickJS fuzz smoke absent: feature OFF\n'
fi
