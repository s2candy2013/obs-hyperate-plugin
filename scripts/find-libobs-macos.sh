#!/usr/bin/env bash
set -euo pipefail

echo "Searching for libobsConfig.cmake..."

paths=(
  "$HOME"
  "/Applications"
  "/opt/homebrew"
  "/usr/local"
)

found=0
for path in "${paths[@]}"; do
  if [[ -d "$path" ]]; then
    while IFS= read -r file; do
      found=1
      echo "$file"
    done < <(find "$path" \( -name libobsConfig.cmake -o -name libobs-config.cmake \) 2>/dev/null)
  fi
done

if [[ "$found" -eq 0 ]]; then
  cat <<'EOF'

No libobs CMake package was found.

That means this Mac currently does not expose OBS development files to CMake.
Installing OBS.app alone normally does not provide libobsConfig.cmake.

Next steps:
  1. Build/install OBS Studio development files, or use an OBS plugin-template dependency setup.
  2. Re-run CMake with either:

     cmake --preset macos-ninja -Dlibobs_DIR="/path/to/directory/containing/libobsConfig.cmake"

     or:

     cmake --preset macos-ninja -DCMAKE_PREFIX_PATH="/path/to/obs/install/prefix"

EOF
fi
