#!/usr/bin/env bash
set -euo pipefail

bundle_path="${1:-}"
if [[ -z "${bundle_path}" ]]; then
  echo "Usage: $0 /path/to/obs-hyperate.plugin" >&2
  exit 2
fi

binary_path="${bundle_path}/Contents/MacOS/obs-hyperate"
frameworks_dir="${bundle_path}/Contents/Frameworks"

if [[ ! -f "${binary_path}" ]]; then
  echo "Plugin binary not found: ${binary_path}" >&2
  exit 1
fi

mkdir -p "${frameworks_dir}"

find_linked_library() {
  local image_path="$1"
  local pattern="$2"

  otool -L "${image_path}" | awk -v pattern="${pattern}" '$1 ~ pattern { print $1; exit }'
}

copy_library() {
  local source_path="$1"
  local library_name="$2"
  local destination_path="${frameworks_dir}/${library_name}"

  if [[ -z "${source_path}" ]]; then
    echo "Could not determine path for ${library_name}" >&2
    exit 1
  fi

  if [[ ! -f "${source_path}" ]]; then
    echo "Required runtime library not found: ${source_path}" >&2
    exit 1
  fi

  cp -fL "${source_path}" "${destination_path}"
  chmod u+w "${destination_path}"
}

change_library_reference() {
  local image_path="$1"
  local old_path="$2"
  local new_path="$3"

  if [[ -n "${old_path}" ]]; then
    install_name_tool -change "${old_path}" "${new_path}" "${image_path}"
  fi
}

set_library_id() {
  local image_path="$1"
  local library_name="$2"

  install_name_tool -id "@rpath/${library_name}" "${image_path}"
}

libwebsockets_path="$(find_linked_library "${binary_path}" 'libwebsockets[^/]*[.]dylib')"
libssl_path="$(find_linked_library "${binary_path}" 'libssl[.]3[.]dylib')"
libcrypto_path="$(find_linked_library "${binary_path}" 'libcrypto[.]3[.]dylib')"

libwebsockets_name="$(basename "${libwebsockets_path}")"
libssl_name="$(basename "${libssl_path}")"
libcrypto_name="$(basename "${libcrypto_path}")"

copy_library "${libwebsockets_path}" "${libwebsockets_name}"
copy_library "${libssl_path}" "${libssl_name}"
copy_library "${libcrypto_path}" "${libcrypto_name}"

bundled_libwebsockets="${frameworks_dir}/${libwebsockets_name}"
bundled_libssl="${frameworks_dir}/${libssl_name}"
bundled_libcrypto="${frameworks_dir}/${libcrypto_name}"

libwebsockets_ssl_path="$(find_linked_library "${bundled_libwebsockets}" 'libssl[.]3[.]dylib')"
libwebsockets_crypto_path="$(find_linked_library "${bundled_libwebsockets}" 'libcrypto[.]3[.]dylib')"
libssl_crypto_path="$(find_linked_library "${bundled_libssl}" 'libcrypto[.]3[.]dylib')"

set_library_id "${bundled_libwebsockets}" "${libwebsockets_name}"
set_library_id "${bundled_libssl}" "${libssl_name}"
set_library_id "${bundled_libcrypto}" "${libcrypto_name}"

change_library_reference   "${binary_path}"   "${libwebsockets_path}"   "@loader_path/../Frameworks/${libwebsockets_name}"
change_library_reference   "${binary_path}"   "${libssl_path}"   "@loader_path/../Frameworks/${libssl_name}"
change_library_reference   "${binary_path}"   "${libcrypto_path}"   "@loader_path/../Frameworks/${libcrypto_name}"

change_library_reference "${bundled_libwebsockets}" "${libwebsockets_ssl_path}" "@loader_path/${libssl_name}"
change_library_reference "${bundled_libwebsockets}" "${libwebsockets_crypto_path}" "@loader_path/${libcrypto_name}"
change_library_reference "${bundled_libssl}" "${libssl_crypto_path}" "@loader_path/${libcrypto_name}"

if command -v codesign >/dev/null 2>&1; then
  codesign --force --sign - "${bundled_libcrypto}" "${bundled_libssl}" "${bundled_libwebsockets}" "${binary_path}"
fi

if otool -L "${binary_path}" "${bundled_libwebsockets}" "${bundled_libssl}" "${bundled_libcrypto}"   | grep -E '/opt/homebrew|/usr/local/opt|/usr/local/Cellar' >/dev/null; then
  echo "Bundled macOS plugin still references Homebrew libraries:" >&2
  otool -L "${binary_path}" "${bundled_libwebsockets}" "${bundled_libssl}" "${bundled_libcrypto}" >&2
  exit 1
fi

echo "Bundled macOS runtime libraries in ${frameworks_dir}:"
ls -1 "${frameworks_dir}"
