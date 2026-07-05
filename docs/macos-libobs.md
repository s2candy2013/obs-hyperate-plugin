# macOS: Fixing `Could not find libobs`

If `cmake --preset macos-ninja` fails with:

```text
Could not find a package configuration file provided by "libobs"
```

the plugin build is working, but CMake cannot find the OBS Studio development files.

Installing OBS as `/Applications/OBS.app` is usually not enough. Native OBS plugins need the OBS headers, libraries and CMake package files, especially `libobsConfig.cmake`.

## Check Your Machine

From the plugin directory:

```sh
bash scripts/find-libobs-macos.sh
```

If it prints a `libobsConfig.cmake` path, configure using the directory that contains that file:

```sh
cmake --preset macos-ninja \
  -Dlibobs_DIR="/path/to/directory/containing/libobsConfig.cmake"
```

If it finds an OBS install prefix instead, use:

```sh
cmake --preset macos-ninja \
  -DCMAKE_PREFIX_PATH="/path/to/obs/install/prefix"
```

## If Nothing Is Found

You need one of these before this plugin can compile:

- A local OBS Studio source build that exports `libobsConfig.cmake`.
- An OBS plugin-template dependency setup that provides OBS sources and prebuilt OBS dependencies.
- A packaged OBS development SDK, if available for your target OBS version.

The HypeRate plugin currently tracks OBS `31.1.1` in `buildspec.json`.

## Local OBS Build Helper

This repository includes helper scripts for a local macOS development setup.

First download and unpack the matching OBS Studio source and official OBS macOS dependency archive:

```sh
bash scripts/setup-obs-dev-macos.sh
```

Then build local `libobs` development files under `.deps/obs-build`:

```sh
bash scripts/build-local-libobs-macos.sh
```

If the build succeeds, the script prints the exact `cmake --preset macos-ninja -Dlibobs_DIR=...` command for this plugin.

This build can take a while. It is intentionally stored in `.deps/`, which is ignored by git.

OBS Studio requires the Xcode generator for its own macOS build. The helper script handles this automatically and removes a previously-created Ninja OBS build directory if needed.

The helper builds only the OBS core development library (`libobs`) and disables OBS's bundled frontend/plugin build. That avoids requiring Qt UI tools and source submodules such as `obs-browser` and `obs-websocket`, which are not included in GitHub's source tarball.
