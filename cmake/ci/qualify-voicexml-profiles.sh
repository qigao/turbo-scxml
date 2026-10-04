#!/usr/bin/env bash
set -euo pipefail

workspace="${GITHUB_WORKSPACE:-$(pwd)}"
: "${VCPKG_ROOT:?VCPKG_ROOT is required}"

noquick_root="$workspace/vcpkg_installed-noquick"
quickjs_root="$workspace/vcpkg_installed"

rm -rf "$noquick_root"
"$VCPKG_ROOT/vcpkg" install \
  --x-manifest-root=. \
  --x-install-root="$noquick_root" \
  --triplet x64-linux \
  --only-binarycaching

configure_profile() {
  local name="$1"
  local installed="$2"
  shift 2
  local build="$workspace/build/profile-$name"
  local stage="$workspace/stage/profile-$name"

  rm -rf "$build" "$stage"
  cmake -S . -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=x64-linux \
    -DVCPKG_INSTALLED_DIR="$installed" \
    -DVCPKG_MANIFEST_MODE=OFF \
    -DBUILD_TESTING=OFF \
    -DTURBOSCXML_ENABLE_PLUGIN=OFF \
    -DCMAKE_INSTALL_PREFIX="$stage" \
    "$@"
  cmake --build "$build" --parallel 2
  cmake --install "$build"
}

consume_profile() {
  local name="$1"
  local installed="$2"
  shift 2
  local build="$workspace/build/consumer-profile-$name"
  local stage="$workspace/stage/profile-$name"

  rm -rf "$build"
  export TURBOSCXML_ROOT="$stage"
  cmake -S tests/install_consumer -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_TARGET_TRIPLET=x64-linux \
    -DVCPKG_INSTALLED_DIR="$installed" \
    -DVCPKG_MANIFEST_MODE=OFF \
    "$@"
  cmake --build "$build" --parallel 2
}

configure_profile core "$noquick_root" \
  -DTURBOSCXML_ENABLE_VOICEXML_CMETA=OFF \
  -DTURBOSCXML_ENABLE_QUICKJS=OFF \
  -DTURBOSCXML_ENABLE_CHTTP_RESOURCE=OFF \
  -DTURBOSCXML_ENABLE_CHTTP_EVENT_IO=OFF
consume_profile core "$noquick_root" \
  -DTURBOSCXML_INSTALL_CONSUMER_VOICEXML_ONLY=ON \
  -DTURBOSCXML_INSTALL_CONSUMER_FORBID_QJS_DISCOVERY=ON

configure_profile cmeta "$noquick_root" \
  -DTURBOSCXML_ENABLE_VOICEXML_CMETA=ON \
  -DTURBOSCXML_ENABLE_QUICKJS=OFF \
  -DTURBOSCXML_ENABLE_CHTTP_RESOURCE=OFF \
  -DTURBOSCXML_ENABLE_CHTTP_EVENT_IO=OFF
consume_profile cmeta "$noquick_root" \
  -DTURBOSCXML_INSTALL_CONSUMER_VOICEXML_CMETA_ONLY=ON \
  -DTURBOSCXML_INSTALL_CONSUMER_FORBID_QJS_DISCOVERY=ON

configure_profile quickjs "$quickjs_root" \
  -DTURBOSCXML_ENABLE_VOICEXML_CMETA=OFF \
  -DTURBOSCXML_ENABLE_QUICKJS=ON \
  -DTURBOSCXML_ENABLE_CHTTP_RESOURCE=OFF \
  -DTURBOSCXML_ENABLE_CHTTP_EVENT_IO=OFF
consume_profile quickjs "$quickjs_root" \
  -DTURBOSCXML_INSTALL_CONSUMER_EXPECT_VOICEXML_QUICKJS=ON

configure_profile chttp "$noquick_root" \
  -DTURBOSCXML_ENABLE_VOICEXML_CMETA=OFF \
  -DTURBOSCXML_ENABLE_QUICKJS=OFF \
  -DTURBOSCXML_ENABLE_CHTTP_RESOURCE=ON \
  -DTURBOSCXML_ENABLE_CHTTP_EVENT_IO=ON
consume_profile chttp "$noquick_root" \
  -DTURBOSCXML_INSTALL_CONSUMER_EXPECT_CHTTP_RESOURCE=ON \
  -DTURBOSCXML_INSTALL_CONSUMER_EXPECT_VOICEXML_CHTTP_RESOURCE=ON \
  -DTURBOSCXML_INSTALL_CONSUMER_EXPECT_CHTTP_EVENT_IO=ON \
  -DTURBOSCXML_INSTALL_CONSUMER_EXPECT_VOICEXML_DIALOG_MANAGER=ON \
  -DTURBOSCXML_INSTALL_CONSUMER_EXPECT_VOICEXML_DOCUMENT_STORE=ON
