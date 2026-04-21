# Jam-PT

Jam-PT is a macOS audio effect plugin built with JUCE and CMake. It currently targets Audio Unit and Standalone formats and includes an initial Core ML loading pipeline for experimenting with model-driven audio processing.

## Status

This repository is an early scaffold, not a finished effect. The current code can:

- build a JUCE plugin for `AU` and `Standalone`
- load an audio file through a simple playback path
- load Core ML models in `.mlmodel`, `.mlpackage`, or `.mlmodelc` form
- prepare a native Objective-C++ bridge for future inference

The Core ML model is not yet applied to the audio buffer during playback. The processing hook exists in `src/CoreMLProcessor.mm`, but the actual tensor conversion and prediction path still need implementation.

## Tech Stack

- C++17 / Objective-C++
- JUCE 8
- CMake 3.22+
- Xcode / Apple toolchain
- Core ML

## Repository Layout

- `CMakeLists.txt`: main project definition
- `src/PluginProcessor.*`: JUCE processor lifecycle and state
- `src/PluginEditor.*`: plugin UI
- `src/AudioFilePlayer.*`: local audio-file playback support
- `src/CoreMLProcessor.*`: Core ML model loading and future inference entry point
- `Models/`: optional local models for development
- `Resources/`: app and plugin resources

## Requirements

- macOS
- Xcode with Command Line Tools
- CMake 3.22 or newer

JUCE can be fetched automatically during configure, so a separate global installation is not required unless you want to use a local checkout.

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

- no model inference is performed on the live audio buffer yet
- state restoration currently persists displayed file names only
- no production-ready parameter mapping or model contract is defined yet
- no automated tests are included yet

## License

Released under the MIT License. See [LICENSE](LICENSE).
