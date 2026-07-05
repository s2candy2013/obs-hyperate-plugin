#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="$ROOT_DIR/.deps"
OBS_VERSION="31.1.1"
OBS_DEPS_VERSION="2025-07-11"

OBS_SOURCE_DIR="$DEPS_DIR/obs-studio-$OBS_VERSION"
OBS_DEPS_DIR="$DEPS_DIR/obs-deps-$OBS_DEPS_VERSION-universal"
OBS_BUILD_DIR="$DEPS_DIR/obs-build"
OBS_INSTALL_DIR="$DEPS_DIR/obs-install"

if [[ ! -d "$OBS_SOURCE_DIR" || ! -d "$OBS_DEPS_DIR" ]]; then
  echo "Missing OBS source/deps. Run this first:" >&2
  echo "  bash scripts/setup-obs-dev-macos.sh" >&2
  exit 1
fi

print_libobs_command() {
  local config="$1"
  local libobs_dir

  libobs_dir="$(dirname "$config")"

  cat <<EOF

Local libobs is ready:
  $config

Configure this plugin with:
  cmake --preset macos-ninja -Dlibobs_DIR="$libobs_dir"

Then build:
  cmake --build --preset macos-ninja

EOF
}

EXISTING_LIBOBS_CONFIG="$(find "$OBS_BUILD_DIR" "$OBS_INSTALL_DIR" \( -name libobsConfig.cmake -o -name libobs-config.cmake \) 2>/dev/null | head -n 1 || true)"
if [[ -n "$EXISTING_LIBOBS_CONFIG" ]]; then
  print_libobs_command "$EXISTING_LIBOBS_CONFIG"
  exit 0
fi

if [[ -f "$OBS_BUILD_DIR/CMakeCache.txt" ]] && grep -q "CMAKE_GENERATOR:INTERNAL=Ninja" "$OBS_BUILD_DIR/CMakeCache.txt"; then
  echo "Removing previous Ninja OBS build directory. OBS requires the Xcode generator on macOS."
  rm -rf "$OBS_BUILD_DIR"
fi

cmake \
  -S "$OBS_SOURCE_DIR" \
  -B "$OBS_BUILD_DIR" \
  -G Xcode \
  -DCMAKE_CONFIGURATION_TYPES=RelWithDebInfo \
  -DCMAKE_PREFIX_PATH="$OBS_DEPS_DIR" \
  -DCMAKE_INSTALL_PREFIX="$OBS_INSTALL_DIR" \
  -DENABLE_UI=OFF \
  -DENABLE_FRONTEND=OFF \
  -DENABLE_PLUGINS=OFF \
  -DENABLE_BROWSER=OFF \
  -DENABLE_SCRIPTING=OFF \
  -DENABLE_AJA=OFF \
  -DENABLE_DECKLINK=OFF \
  -DENABLE_WEBRTC=OFF \
  -DENABLE_VLC=OFF

cmake --build "$OBS_BUILD_DIR" --target libobs --config RelWithDebInfo

LIBOBS_CONFIG="$(find "$OBS_INSTALL_DIR" "$OBS_BUILD_DIR" \( -name libobsConfig.cmake -o -name libobs-config.cmake \) 2>/dev/null | head -n 1 || true)"

if [[ -z "$LIBOBS_CONFIG" ]]; then
  echo "Built OBS, but did not find libobsConfig.cmake automatically." >&2
  echo "Try:" >&2
  echo "  find \"$OBS_INSTALL_DIR\" \"$OBS_BUILD_DIR\" -name 'libobs*Config.cmake'" >&2
  exit 1
fi

print_libobs_command "$LIBOBS_CONFIG"
