# Release and Installation

This project ships native OBS plugin builds as ZIP files attached to GitHub Actions runs and draft GitHub Releases.

## Creating a Release

The GitHub Actions workflow in `.github/workflows/release.yml` runs in two ways:

- Manually from GitHub Actions: **Build and Release** -> **Run workflow**
- Automatically for tags starting with `v`, for example:

```sh
git tag v0.1.0
git push origin v0.1.0
```

For tag builds, the workflow creates a draft GitHub Release and attaches platform ZIP files.

## HypeRate API Token

Streamers should not need to enter an API token manually.

The plugin currently compiles a bundled HypeRate API token into private builds. The workflow also supports an optional repository secret:

```text
HYPERATE_API_TOKEN
```

If that secret is present, CI uses it at build time. If it is missing, the development fallback token in `CMakeLists.txt` is used.

For public distribution, use a dedicated public/plugin token with limited permissions, not a personal or admin token.

## User Installation

Users should close OBS before installing or replacing the plugin.

### macOS

Download:

```text
obs-hyperate-<version>-macos.zip
```

Unzip it and copy:

```text
obs-hyperate.plugin
```

to:

```text
~/Library/Application Support/obs-studio/plugins/
```

Then restart OBS.

### Windows

Download:

```text
obs-hyperate-<version>-windows-x64.zip
```

Unzip it. Copy the contained:

```text
obs-hyperate
```

folder to:

```text
%APPDATA%\obs-studio\plugins\
```

The final layout should look like:

```text
%APPDATA%\obs-studio\plugins\obs-hyperate\bin\64bit\obs-hyperate.dll
%APPDATA%\obs-studio\plugins\obs-hyperate\data\locale\de-DE.ini
```

Then restart OBS.

### Linux

Download:

```text
obs-hyperate-<version>-linux-x86_64.zip
```

Unzip it. Copy the contained:

```text
obs-hyperate
```

folder to:

```text
~/.config/obs-studio/plugins/
```

The final layout should look like:

```text
~/.config/obs-studio/plugins/obs-hyperate/bin/64bit/obs-hyperate.so
~/.config/obs-studio/plugins/obs-hyperate/data/locale/de-DE.ini
```

Then restart OBS.

## Current Packaging Notes

- macOS builds are not signed or notarized yet.
- Windows builds are not code-signed yet.
- Linux builds target Ubuntu 24.04 x86_64 first.
- The workflow is a first release pipeline and may need one or two CI iterations to match OBS runner dependency details exactly.
