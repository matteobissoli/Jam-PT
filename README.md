# Jam-PT

Jam-PT is a macOS audio effect plugin built with JUCE and CMake. It targets Audio Unit and Standalone formats and uses the external `demucs` command-line tool to render four stems from a loaded audio file, then mixes them back in real time inside the plugin.

## Status

This repository is an implementation scaffold for a stem-separation AU. The current code can:

- build a JUCE plugin for `AU` and `Standalone`
- load an audio file into the plugin
- select a Demucs model name exposed by the installed `demucs` CLI
- separate the file into `vocals`, `drums`, `bass`, and `other` stems on a background thread
- cache rendered stems under `~/Library/Application Support/Jam-PT/DemucsCache/<model>/<source-key>/`
- reuse cached stems when the same file is loaded again with the same model
- preserve playback, waveform scrubbing, progress reporting, and per-stem gain control in the plugin UI

## Tech Stack

- C++17 / Objective-C++
- JUCE 8
- CMake 3.22+
- Xcode / Apple toolchain
- external `demucs` CLI

## Repository Layout

- `CMakeLists.txt`: main project definition
- `src/PluginProcessor.*`: JUCE processor lifecycle and state
- `src/PluginEditor.*`: plugin UI
- `src/AudioFilePlayer.*`: local audio-file playback support
- `src/DemucsProcessor.*`: Demucs CLI orchestration, cache management, stem loading, and final stem mixing
- `Models/`: optional local models for development
- `Resources/`: app and plugin resources

## Requirements

- macOS
- Xcode with Command Line Tools
- CMake 3.22 or newer
- a working `demucs` CLI installation available to the host process

JUCE can be fetched automatically during configure, so a separate global installation is not required unless you want to use a local checkout.

## Demucs Setup

Jam-PT does not embed Demucs. The plugin launches the external `demucs` executable, waits for it to render stems, and then loads the cached `.wav` files from:

```text
~/Library/Application Support/Jam-PT/DemucsCache/
```

Because of this, the most important part of setup is installing a Demucs runtime that is actually usable from both Terminal and the plugin host.

### Recommended setup on macOS

1. Install Homebrew dependencies:

```bash
brew install python@3.12 ffmpeg
```

If you prefer, `python@3.11` is also a good choice. Avoid relying on Python 3.14 for this stack unless you already know your local `torch`/`torchaudio`/`torchcodec` combination works.

2. Install `pipx` if needed:

```bash
brew install pipx
pipx ensurepath
```

After `pipx ensurepath`, restart Terminal so `~/.local/bin` is available in your shell `PATH`.

3. Remove any broken older Demucs environment:

```bash
pipx uninstall demucs
```

4. Install Demucs with an explicit Python interpreter:

```bash
pipx install --python /opt/homebrew/bin/python3.12 demucs
```

If you installed `python@3.11`, use `/opt/homebrew/bin/python3.11` instead.

5. Inject `torchcodec`, which recent `torchaudio`-based loading may require:

```bash
pipx inject demucs torchcodec
```

### Verify the runtime before launching the plugin

Confirm that the executable is visible:

```bash
which demucs
demucs --help
```

Then run a real separation test from Terminal on a small audio file:

```bash
demucs -n htdemucs "/absolute/path/to/test-file.mp3"
```

If your paths contain spaces, always wrap them in quotes. This is especially important when using output folders such as `~/Library/Application Support/...`.

If this command fails in Terminal, the plugin will fail too. Fix the Demucs runtime first.

### Common Demucs runtime issues

- `FFmpeg is not installed`
  Install it with `brew install ffmpeg`.

- `No module named 'torchcodec'` or `TorchCodec is required`
  Inject it into the `pipx` environment:

```bash
pipx inject demucs torchcodec
```

- `Could not load libtorchcodec` with missing `libavutil.*.dylib`
  This usually means `ffmpeg` is missing or the Demucs environment was created against an incompatible Python / PyTorch toolchain. Reinstall with Python 3.11 or 3.12 and ensure `ffmpeg` is installed first.

- `No executable for the provided Python version 'python3.12' found in PATH`
  Install that Python version with Homebrew and reference the full path, for example `/opt/homebrew/bin/python3.12`.

### Notes for plugin hosts

- The plugin looks for `demucs` in common macOS locations such as `~/.local/bin`, `/opt/homebrew/bin`, and `/usr/local/bin`, then falls back to the host `PATH`.
- The plugin does not require Demucs models or stems to be manually copied into the project.
- Once a file has been separated successfully, the cached stems are reused automatically for the same source file and model combination.

## Build

### Option A: fetch JUCE automatically

```bash
cmake -S . -B build -G Xcode -DJAMPT_FETCH_JUCE=ON
cmake --build build --config Release
```

### Option B: use a local JUCE checkout

```bash
cmake -S . -B build -G Xcode -DJAMPT_FETCH_JUCE=OFF -DJUCE_SOURCE_DIR=/path/to/JUCE
cmake --build build --config Release
```

After configuration, open the generated Xcode project inside `build/` if you want to build, debug, or run the Standalone target from Xcode.

## Install

The CMake target enables `COPY_PLUGIN_AFTER_BUILD`, so the Audio Unit component is copied automatically after a successful build on a standard macOS development setup.

If you need to install manually, the built Audio Unit bundle should be copied to:

```text
~/Library/Audio/Plug-Ins/Components/
```

After installation, rescan plugins in MainStage, Logic Pro, or your preferred AU host if the plugin does not appear immediately.

## MainStage Notes

MainStage loads Audio Unit plugins on macOS, so the `AU` target is the primary deployment format for this project. The `Standalone` target is included only to simplify local development and testing outside a host.

## Current Limitations

- separation is performed offline per loaded file, so long tracks can still require substantial RAM and wait time
- the plugin depends on a working external `demucs` runtime with compatible Python, `ffmpeg`, and `torchcodec`
- cache invalidation is based on source path, file size, and modification timestamp
- no automated tests are included yet

## License

Released under the MIT License. See [LICENSE](LICENSE).
