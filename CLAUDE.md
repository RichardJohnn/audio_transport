# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

Audio Transport is a C++ library that implements a novel audio effect for transitioning between signals using optimal transport. The effect creates musical glides (portamento) where pitches in one audio signal slide to pitches in another by solving a 1-dimensional optimal transport problem. Published in DAFx 2019 (Best Student Paper).

## Build System

This project uses CMake with C++11:

**Install dependencies:**
```bash
# System dependencies (via package manager)
# fftw3 - Required for all builds
# ffmpeg - Required only for examples
# audiorw - Required only for examples (install from https://github.com/sportdeath/audiorw)
```

**Build library only:**
```bash
mkdir build && cd build
cmake ..
make
sudo make install  # Installs to system lib and include directories
```

**Build with examples:**
```bash
mkdir build && cd build
cmake .. -D BUILD_EXAMPLES=ON
make
```

Binaries are built in the `build/` directory (not installed).

## Architecture

### Core Processing Pipeline

The library operates on audio in three stages:

1. **Analysis** (`spectral.hpp/cpp`): Converts time-domain audio to spectral representation
   - Uses STFT with Hann windows (COLA property)
   - Computes reassigned time and frequency for each spectral point
   - Each `spectral::point` contains complex value, time, freq, and reassigned time/freq

2. **Interpolation** (`audio_transport.hpp/cpp`): Performs optimal transport-based morphing
   - Groups spectral points into `spectral_mass` objects (left_bin, center_bin, right_bin, mass)
   - Solves 1D optimal transport via `transport_matrix()` (greedy algorithm)
   - Interpolates spectral masses between left and right inputs
   - Maintains phase coherence across windows using phase vector state

3. **Synthesis** (`spectral.hpp/cpp`): Converts back to time domain
   - Inverse STFT with overlap-add

### Equal Loudness Weighting

`equal_loudness.hpp/cpp` implements A-weighting for perceptually uniform interpolation. Apply before transport, remove after.

### Key Data Structures

- `spectral::point`: Complex value + time/frequency + reassigned time/frequency
- `spectral_mass`: Grouped spectral region with left/center/right bin indices and total mass
- Transport matrix: Vector of tuples `(left_index, right_index, mass_amount)`

### Window Processing

- Default window size: 0.05 seconds (50ms)
- Padding multiplier: 7x (for frequency resolution)
- Overlap: Configurable for COLA reconstruction
- Phase continuity maintained via `phases` vector passed through interpolation calls

## Example Usage Patterns

**Transport effect** (morph between two files):
```bash
./transport piano.wav guitar.mp3 20 70 out.flac
# 0-20%: piano only
# 20-70%: morph piano → guitar
# 70-100%: guitar only
```

**Glide effect** (feedback-based portamento):
```bash
./glide piano.wav 1 piano_glide.ogg
# Apply 1ms time constant glide
```

## Using in External Projects

Library-only install requires only `fftw3`. Link against `audio_transport` and include:
- `<audio_transport/spectral.hpp>` - Analysis/synthesis
- `<audio_transport/audio_transport.hpp>` - Interpolation
- `<audio_transport/equal_loudness.hpp>` - Perceptual weighting (optional)

Typical workflow:
1. Call `spectral::analysis()` on input audio
2. Apply `equal_loudness::apply()` to spectral frames
3. Call `interpolate()` per window with interpolation factor [0,1]
4. Apply `equal_loudness::remove()` to output frames
5. Call `spectral::synthesis()` to reconstruct audio

Phase coherence requires maintaining a `std::vector<double> phases` buffer across windows.
