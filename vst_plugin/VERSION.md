# Audio Transport VST3 - Version History

## v1.3.0 (Current) - 2026-11-13

### New Features
- **Extreme parameter ranges for experimentation**:
  - **Window Size**: Expanded from 20-200ms to **1ms-500ms**
    - 1ms = extreme glitch effects
    - 500ms = ultra-smooth spectral morphing (matches original offline algorithm)
  - **Overlap control**: Choose 50%, 75%, 87.5%, or 93.75% window overlap
    - Higher overlap = smoother but more latency and CPU
  - **FFT Size multiplier**: 2x, 4x, 8x, 16x, 32x, 64x, or **128x** zero-padding
    - Higher = smoother spectral interpolation (original offline used 128x)
    - Can create multi-second latencies at extreme settings
- **Philosophy shift**: Plugin optimized for fast experimentation rather than live performance
  - Latency can reach several seconds at extreme settings, but still faster than CLI workflow
  - All parameters properly tracked and reported to DAW
  - Window size slider uses logarithmic scale for better control across huge range

### Technical
- Both algorithms (CDF and Reassignment) support full parameter range
- Processor rebuilds when spectral parameters change, on a background thread, and is swapped in on the audio thread
- Reassignment algorithm now uses the same STFT sizing as the offline `transport -f` (hop = N/hop_div, FFT = nextpow2(N)·fft_mult)
- Latency automatically updates and compensates for extreme settings

### Known limitations
- Reassignment output does not match `transport -f` at the same parameters: the realtime path applies no equal-loudness weighting, while `-f` does. Parameter meanings (hop and FFT size) do match.
- Extreme settings (e.g. 500 ms / 128x) cannot play live: each hop runs several FFTs of millions of points inside one audio callback, so playback drops out regardless of reported latency. Use moderate settings live and an offline bounce for extreme ones.
- Changing window, overlap, FFT size or algorithm builds a new processor (allocating buffers and planning FFTs, not recompiling) on a background thread. The plugin outputs silence, including the dry signal, until it is ready, and the reported latency changes. With CDF at large FFT sizes this can take seconds, so switching algorithms is no longer an instant A/B comparison.
- Not yet tested in a DAW.

---

## v1.2.0 - 2024-02-11

### New Features
- **Algorithm selector**: Choose between two optimal transport algorithms
  - **CDF (Clean)**: Simple CDF-based optimal transport - clean morphing, lower CPU usage
  - **Reassignment (Glitchy)**: Spectral reassignment-based algorithm - creates interesting glitchy artifacts, higher CPU usage
  - Reassignment mode implements Henderson & Solomon (DAFx 2019) algorithm with reassigned spectrograms
  - Window size has dramatic effect on pitch/timbre in Reassignment mode
  - Both algorithms properly track and report latency to DAW
  - Algorithms can be switched in real-time for creative A/B comparison

### Technical
- Added `RealtimeReassignmentTransport` class for reassignment-based processing
- Plugin now maintains both processors for instant algorithm switching
- Latency automatically updates when switching algorithms
- Delay buffers adjust to match current algorithm's latency

---

## v1.1.1 - 2024-02-11

### Bug Fixes
- **Fixed slider interaction**: Horizontal sliders (Window Size, Dry/Wet) now respond to mouse dragging properly
- **Fixed latency compensation**: Dry signals are now internally delayed to match processing latency
  - Eliminates "slap delay" effect when mixing dry and processed signals
  - Dry signals at extremes (0.0 and 1.0) are now time-aligned with morphed output
  - Plugin still reports latency to DAW for overall track alignment
- **Fixed gain compensation**: Applied -3dB reduction around k=1.0 to prevent loudness spike at morph=0.5

### Technical
- Added internal delay buffers for main and sidechain dry signals
- Delay buffer size automatically matches transport processor latency
- Circular buffer implementation for efficient delay
- Improved parameter update logic to prevent UI conflicts

---

## v1.1.0 - 2024-02-11

### New Features
- **Morph Mode selector**: Choose between "Full Morph" and "Dry at Extremes"
  - **Full Morph**: Continuous optimal transport from main to sidechain (k: 0→1)
  - **Dry at Extremes**: Dry signals at extremes with input flip at 0.5
    - 0.0 = Dry main input
    - 0.0→0.5 = Morph main→sidechain (k: 0→1)
    - 0.5 = Flip point (inputs swap)
    - 0.5→1.0 = Morph sidechain→main (k: 1→0 with flipped inputs)
    - 1.0 = Dry sidechain input

- **Dry/Wet control**: Blend between dry main input and processed output (0-100%)
  - Enables parallel processing
  - 100% = full effect, 0% = bypass

- **Version display**: Shows plugin version in UI

### Improvements
- Updated UI layout to accommodate new controls
- Dynamic help text that changes based on morph mode
- Better parameter descriptions

### Technical
- ARM64 native build for Apple Silicon
- JUCE 8.0.4 (macOS 15 compatible)
- Improved state save/restore with backward compatibility

---

## v1.0.0 - Initial Release

### Core Features
- Real-time spectral morphing using optimal transport
- Sidechain input support
- Main morph control (0.0 = main, 1.0 = sidechain)
- Window size control (20-200ms)
- Bypass button
- Automatic latency compensation
- VST3, AU, and Standalone formats

### Technical
- Based on Henderson & Solomon (DAFx 2019) algorithm
- CDF-based 1D optimal transport
- FFTW3 for efficient FFT operations
- Mono processing (stereo planned for future)
- Configurable quality/latency tradeoff

---

## Planned Features

### v1.2.0 (Future)
- [ ] Stereo support (L/R or Mid/Side)
- [ ] Preset system
- [ ] Visual spectrum analyzer
- [ ] Transport map visualization
- [ ] MIDI learn for parameters
- [ ] Additional morph curve shapes

### v1.3.0 (Future)
- [ ] Multi-band processing option
- [ ] Frequency range control
- [ ] Morphing speed/smoothing control
- [ ] A/B comparison mode

---

## Version Numbering Scheme

- **Major**: Breaking changes, major new features
- **Minor**: New features, improvements (backward compatible)
- **Patch**: Bug fixes, small tweaks

Current: **v1.1.0**
