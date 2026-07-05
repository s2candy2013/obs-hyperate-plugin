#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEPS_DIR="$ROOT_DIR/.deps"
DOWNLOADS_DIR="$DEPS_DIR/downloads"
OBS_VERSION="31.1.1"
OBS_DEPS_VERSION="2025-07-11"
OBS_DEPS_HASH="495687e63383d1a287684b6e2e9bfe246bb8f156fe265926afb1a325af1edd2a"

OBS_SOURCE_DIR="$DEPS_DIR/obs-studio-$OBS_VERSION"
OBS_DEPS_DIR="$DEPS_DIR/obs-deps-$OBS_DEPS_VERSION-universal"

mkdir -p "$DOWNLOADS_DIR"

download() {
  local url="$1"
  local output="$2"

  if [[ -f "$output" ]]; then
    echo "Using existing $output"
    return
  fi

  echo "Downloading $url"
  curl -L "$url" -o "$output"
}

verify_sha256() {
  local file="$1"
  local expected="$2"
  local actual

  actual="$(shasum -a 256 "$file" | awk '{print $1}')"
  if [[ "$actual" != "$expected" ]]; then
    echo "Checksum mismatch for $file" >&2
    echo "expected: $expected" >&2
    echo "actual:   $actual" >&2
    exit 1
  fi
}

OBS_TARBALL="$DOWNLOADS_DIR/obs-studio-$OBS_VERSION.tar.gz"
OBS_DEPS_TARBALL="$DOWNLOADS_DIR/macos-deps-$OBS_DEPS_VERSION-universal.tar.xz"

download \
  "https://github.com/obsproject/obs-studio/archive/refs/tags/$OBS_VERSION.tar.gz" \
  "$OBS_TARBALL"

download \
  "https://github.com/obsproject/obs-deps/releases/download/$OBS_DEPS_VERSION/macos-deps-$OBS_DEPS_VERSION-universal.tar.xz" \
  "$OBS_DEPS_TARBALL"

echo "Verifying OBS deps checksum..."
verify_sha256 "$OBS_DEPS_TARBALL" "$OBS_DEPS_HASH"

if [[ ! -d "$OBS_SOURCE_DIR" ]]; then
  echo "Extracting OBS Studio sources to $OBS_SOURCE_DIR"
  mkdir -p "$OBS_SOURCE_DIR"
  tar -xzf "$OBS_TARBALL" --strip-components=1 -C "$OBS_SOURCE_DIR"
fi

if [[ ! -d "$OBS_DEPS_DIR" ]]; then
  echo "Extracting OBS deps to $OBS_DEPS_DIR"
  mkdir -p "$OBS_DEPS_DIR"
  tar -xJf "$OBS_DEPS_TARBALL" --strip-components=1 -C "$OBS_DEPS_DIR"
fi

cat <<EOF

OBS development inputs are ready:
  OBS source: $OBS_SOURCE_DIR
  OBS deps:   $OBS_DEPS_DIR

Next:
  bash scripts/build-local-libobs-macos.sh

EOF
