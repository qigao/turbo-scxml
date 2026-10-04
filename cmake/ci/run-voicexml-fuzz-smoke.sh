#!/usr/bin/env bash
set -euo pipefail

build_dir="${GITHUB_WORKSPACE:-$PWD}/build/ci-sanitizers/tests"
base="$build_dir/voicexml_base_fuzz_smoke"
cmeta="$build_dir/voicexml_cmeta_fuzz_smoke"

for target in "$base" "$cmeta"; do
  if [ ! -x "$target" ]; then
    printf 'missing VoiceXML fuzz smoke target: %s\n' "$target" >&2
    exit 1
  fi
  printf 'running bounded fuzz smoke: %s\n' "$target"
  timeout --signal=KILL 30s "$target"
done
