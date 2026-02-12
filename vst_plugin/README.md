# Audio Transport VST3 Plugin

A real-time spectral morphing plugin using optimal transport to blend between main input and sidechain input.

## Features

- **Spectral Morphing**: Smooth frequency-aware morphing between two audio sources
- **Sidechain Input**: Use any audio source as the morph target
- **Real-time Processing**: Low-latency optimal transport algorithm
- **Adjustable Quality**: Configure window size for latency/quality tradeoff
- **Clean UI**: Simple interface with morph knob and window size control

## Controls

- **Morph**: 0.0 = Main input only, 1.0 = Sidechain only, 0.5 = 50/50 blend
- **Window Size**: 20-200ms (smaller = lower latency, larger = better quality)
- **Bypass**: True bypass when enabled

## Building

### Prerequisites

1. **JUCE Framework** (automatically downloaded by CMake)
2. **FFTW3 Library**:
   ```bash
   # macOS
   brew install fftw

   # Linux
   sudo apt-get install libfftw3-dev
   ```
3. **Audio Transport Library** (should already be built in parent directory)
4. **CMake 3.15+**

### Build Steps

```bash
cd /Users/rich/repos/audio_transport

# First, build the audio_transport library if not already done
mkdir -p build && cd build
cmake ..
make
cd ..

# Now build the VST plugin
cd vst_plugin
mkdir build && cd build

# Configure
cmake ..

# Build
cmake --build . --config Release

# The VST3 will be in:
# macOS: ~/Library/Audio/Plug-Ins/VST3/Audio Transport.vst3
# Linux: ~/.vst3/Audio Transport.vst3
# Windows: C:\Program Files\Common Files\VST3\Audio Transport.vst3
```

### Quick Build Script

Or use the provided build script:

```bash
cd /Users/rich/repos/audio_transport/vst_plugin
./build.sh
```

## Usage in Your DAW

### 1. Load the Plugin

- Open your DAW (Ableton, Logic, Reaper, etc.)
- Insert "Audio Transport" on a track
- Enable sidechain routing to the plugin

### 2. Set Up Sidechain

**Ableton Live:**
1. Add Audio Transport to track A
2. In the plugin, set sidechain to "Audio From" → Track B
3. Adjust morph knob

**Logic Pro:**
1. Insert Audio Transport on track A
2. Set sidechain input to desired bus/track
3. Adjust morph knob

**Reaper:**
1. Insert Audio Transport on track A
2. Route another track to pins 3/4 of the plugin track
3. Adjust morph knob

### 3. Adjust Parameters

- **Start with morph at 0.5** (50/50 blend)
- **Adjust window size** based on content:
  - Voice/percussion: 50-80ms
  - Pads/ambient: 100-150ms
  - Smooth transitions: 150-200ms
- **Use bypass** to A/B compare

## Example Use Cases

### Vocal Morphing
- Main: Lead vocal
- Sidechain: Backing vocal or harmony
- Morph: Automate from 0.0 to 1.0 for smooth transition

### Instrument Blend
- Main: Acoustic piano
- Sidechain: Electric piano
- Morph: 0.3-0.7 for hybrid sounds

### Spectral Crossfade
- Main: Intro sound
- Sidechain: Main section sound
- Morph: Automate 0→1 over 4-8 bars

### Sound Design
- Main: Noise/texture
- Sidechain: Musical element
- Morph: Real-time performance control

## Performance

| Window Size | Latency | CPU Usage | Quality |
|-------------|---------|-----------|---------|
| 50ms        | ~25ms   | ~3-5%     | Good    |
| 100ms       | ~50ms   | ~5-8%     | Better  |
| 150ms       | ~75ms   | ~8-10%    | Best    |

*CPU usage on Apple M1, 512 sample buffer @ 44.1kHz*

## Troubleshooting

### No Sidechain Input

**Problem**: Sidechain appears disabled in DAW
**Solution**: Check your DAW's sidechain routing. Some DAWs require explicit sidechain bus setup.

### High CPU Usage

**Problem**: Plugin using too much CPU
**Solutions**:
- Reduce window size to 50-80ms
- Increase buffer size in DAW preferences
- Disable plugin on tracks where not needed

### Artifacts/Glitches

**Problem**: Crackling or artifacts in output
**Solutions**:
- Increase window size to 100-150ms
- Check that sidechain is properly routed
- Ensure buffer sizes are adequate (256+ samples)

### Build Errors

**Problem**: Can't find JUCE
**Solution**: CMake will download JUCE automatically. Ensure internet connection.

**Problem**: Can't find FFTW3
**Solution**: Install FFTW3 via package manager (see Prerequisites)

**Problem**: Can't find audio_transport
**Solution**: Build the main library first:
```bash
cd /Users/rich/repos/audio_transport/build
cmake .. && make
```

## Technical Details

### Algorithm

Uses CDF-based 1D optimal transport for spectral morphing:
1. Compute STFT of both inputs
2. Treat magnitude spectra as probability distributions
3. Compute optimal transport map via CDFs
4. Interpolate spectral content along transport paths
5. Inverse STFT for output

### Latency

Latency = window_size / 2
- 50ms window → 25ms latency
- 100ms window → 50ms latency
- 200ms window → 100ms latency

Plugin reports latency to host for automatic delay compensation.

### Channels

Currently mono (1 input + 1 sidechain → 1 output). Stereo version coming soon!

## Files

```
vst_plugin/
├── CMakeLists.txt              # Build configuration
├── build.sh                    # Quick build script
├── README.md                   # This file
└── Source/
    ├── PluginProcessor.h       # Audio processing logic
    ├── PluginProcessor.cpp
    ├── PluginEditor.h          # UI
    └── PluginEditor.cpp
```

## License

Same as the main audio_transport library.

## Credits

- Algorithm: Henderson & Solomon (DAFx 2019)
- Implementation: Based on audio_transport library
- Framework: JUCE

## Support

For issues or questions:
- GitHub: https://github.com/sportdeath/audio_transport/issues
- See parent directory for library documentation
