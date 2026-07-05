# OBS HypeRate Plugin

Native heart-rate-powered effects for OBS.

This is a first-version native OBS plugin scaffold for HypeRate. It intentionally focuses on filters that render directly inside OBS instead of becoming a generic automation or rule engine.

## Proposed Architecture

- HypeRate connection module: `src/hyperate/`
  - Owns the WebSocket lifecycle.
  - Joins a configured HypeRate channel/session.
  - Extracts BPM samples and submits them to shared state.
- Heart rate state module: `src/heart_rate/`
  - Stores raw BPM, smoothed BPM, session maximum BPM and zone.
  - Provides thread-safe snapshots for OBS render callbacks.
- OBS UI/settings module: `src/sources/`
  - Provides a non-visual `HypeRate Heart Rate Input` source.
  - Lets the user enter the channel/session ID and connect/disconnect.
  - Shows connection/BPM/zone status in source properties.
- Effect/filter modules: `src/filters/`
  - Native OBS filters read the shared heart-rate state.
  - V1 starts with `Heartbeat Camera Shake`.

The core design is:

1. Add one `HypeRate Heart Rate Input` source to the OBS scene/source list.
2. Enter a HypeRate channel/session ID.
3. Add HypeRate filters to any visual source.
4. Filters react to the latest smoothed BPM without external automation tools.

## File / Folder Structure

```text
CMakePresets.json
CMakeLists.txt
buildspec.json
cmake/
  common/
    bootstrap.cmake
    compilerconfig.cmake
    defaults.cmake
    helpers.cmake
  macos-info.plist.in
data/
  locale/
    en-US.ini
src/
  plugin-main.cpp
  heart_rate/
    heart_rate_state.hpp
    heart_rate_state.cpp
  hyperate/
    hyperate_client.hpp
    hyperate_client.cpp
  sources/
    hyperate_input_source.hpp
    hyperate_input_source.cpp
  filters/
    heartbeat_camera_shake.hpp
    heartbeat_camera_shake.cpp
  util/
    log.hpp
```

## Implemented V1 Pieces

- Minimal native OBS plugin skeleton.
- OBS source registration.
- Non-visual `HypeRate Heart Rate Input` source.
- Thread-safe heart-rate state.
- Basic exponential BPM smoothing.
- Simple heart-rate zones:
  - Resting: below 100 BPM
  - Active: 100-129 BPM
  - High: 130-159 BPM
  - Peak: 160+ BPM
- Session max BPM tracking.
- WebSocket client module using `libwebsockets` when available.
- Explicit Connect / Disconnect controls for the HypeRate input source.
- First native filter: `Heartbeat Camera Shake`.

## Heartbeat Camera Shake

The camera shake filter is an OBS source filter:

- Reads smoothed BPM from shared state.
- Synthesizes heartbeat pulses from BPM.
- Picks a new shake direction per beat.
- Scales shake intensity between threshold BPM and max-intensity BPM.
- Applies the shake by transforming the filter target texture inside OBS render callbacks.

Settings:

- Shake threshold BPM
- Max intensity BPM
- Max shake in pixels
- Motion smoothing
- Heartbeat decay
- Overscan scale
- Test mode and test BPM

## Planned V1 Filters

These are not implemented yet, but the current structure is ready for them:

- `Heartbeat Glow`
  - Pulse brightness/glow with each heartbeat.
  - Intensity maps from min/max BPM.
- `Heartbeat Zoom`
  - Pulse source scale with BPM.
  - Configurable scale range and BPM mapping.
- `Heartbeat Vignette / Pulse Flash`
  - Red/dark overlay pulse.
  - Configurable color, opacity, decay and BPM mapping.

## Build Instructions

The project now follows the current OBS Plugin Template style for local builds:

- `buildspec.json` contains plugin metadata, OBS dependency versions and the macOS bundle ID.
- `CMakePresets.json` provides platform presets.
- `cmake/common/` contains a small local template-compatible bootstrap/helper layer.
- `data/locale/en-US.ini` contains OBS UI strings.

You still need CMake plus OBS/libobs development dependencies available locally. The official OBS template currently documents Visual Studio 2022 on Windows, Xcode 16 on macOS, and CMake/Ninja/pkg-config on Ubuntu 24.04.

If CMake says it cannot find `libobs`, read `docs/macos-libobs.md`. Installing the OBS app alone normally does not expose the native plugin development files.

For end-user ZIP packages and GitHub Releases, read `docs/release-and-installation.md`.

For a local macOS development setup, the shortest path is:

```sh
bash scripts/setup-obs-dev-macos.sh
bash scripts/build-local-libobs-macos.sh
```

The second script prints the `libobs_DIR` value to pass back into the plugin configure command.

### macOS

Install dependencies:

```sh
brew install cmake ninja pkg-config libwebsockets
```

Configure and build:

```sh
cmake --preset macos-ninja \
  -DCMAKE_PREFIX_PATH="/path/to/obs-studio/build;/path/to/obs-deps"
cmake --build --preset macos-ninja
```

For an Xcode universal build:

```sh
cmake --preset macos \
  -DCMAKE_PREFIX_PATH="/path/to/obs-studio/build;/path/to/obs-deps"
cmake --build --preset macos
```

The plugin target is `obs-hyperate.plugin` on macOS.

### Windows

Install:

- Visual Studio 2022 with C++ workload
- CMake
- OBS Studio development environment or OBS plugin template dependencies
- `libwebsockets` through vcpkg or your OBS dependency bundle

Example with vcpkg:

```powershell
vcpkg install libwebsockets:x64-windows
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:\path\to\obs-build
cmake --build build --config RelWithDebInfo
```

Or through the preset:

```powershell
cmake --preset windows-x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:\path\to\vcpkg\scripts\buildsystems\vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=C:\path\to\obs-build
cmake --build --preset windows-x64
```

### Linux

Install typical dependencies:

```sh
sudo apt install cmake ninja-build pkg-config libobs-dev libwebsockets-dev
```

Configure and build:

```sh
cmake --preset ubuntu-x86_64
cmake --build --preset ubuntu-x86_64
sudo cmake --install build
```

If you use the preset binary directory, install with:

```sh
sudo cmake --install build_x86_64
```

## HypeRate Protocol Placeholders

The code isolates all protocol assumptions in `src/hyperate/hyperate_client.cpp`.

The current implementation follows the public HypeRate WebSocket reference:

- Default host: `app.hyperate.io`
- Default path: `/ws/<device_id>`
- Connection URL: `wss://app.hyperate.io/ws/<device_id>?token=<api-key>`
- Join frame:

```json
{"topic":"hr:<channel_id>","event":"phx_join","payload":{},"ref":0}
```

- Keep-alive frame, sent every 15 seconds:

```json
{"event":"ping","payload":{"timestamp":0}}
```

- Leave frame:

```json
{"topic":"hr:<channel_id>","event":"phx_leave","payload":{},"ref":0}
```

- HypeRate BPM payload:

```json
{"event":"hr_update","payload":{"hr":79},"ref":null,"topic":"hr:<channel_id>"}
```

The parser is still intentionally permissive and also accepts `bpm`, `heartRate` and `heart_rate` keys from older examples.

For this private development build, the HypeRate API token is compiled into the input source so the OBS UI does not expose an API-key field. Replace that with secure local configuration before distributing the plugin.

## Notes

- This is intentionally not a replacement for Streamer.bot, Touch Portal, SAMMI or LioranBoard.
- The plugin should stay focused on native OBS rendering effects driven by heart-rate data.
- Generic mappings, hotkeys, scene automation and local WebSocket output are possible later, but they are outside V1.
