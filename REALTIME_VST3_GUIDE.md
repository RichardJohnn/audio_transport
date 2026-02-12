# Real-time Audio Transport for VST3 Plugins

## Overview

`RealtimeAudioTransport` is a simplified, real-time optimized implementation of the audio transport algorithm designed for VST3 plugins with sidechain input. Unlike the original research implementation which uses reassigned spectrograms, this version uses a simpler CDF-based optimal transport approach that's more suitable for real-time processing.

## Key Features

- **Stream-based processing**: Processes small audio buffers incrementally (e.g., 512 samples)
- **Dual input**: Main audio input + sidechain input
- **Low latency**: Configurable latency (default ~100ms with 100ms window)
- **No file I/O**: Works entirely with audio buffers in memory
- **Thread-safe ready**: Can be made thread-safe for VST processing

## Quick Start

### Build Instructions

```bash
# Build library only
mkdir build && cd build
cmake ..
make
sudo make install

# Build with examples (requires audiorw and ffmpeg)
cmake .. -D BUILD_EXAMPLES=ON
make
```

### Run Example

```bash
# Demo mode (generates test tones)
./realtime_transport --demo output.wav 0.5

# File mode
./realtime_transport input.wav sidechain.wav output.wav 0.5
```

## VST3 Plugin Integration

### 1. Include Header

```cpp
#include <audio_transport/RealtimeAudioTransport.hpp>
```

### 2. Add as Member Variable

```cpp
class MyVSTPlugin {
private:
    std::unique_ptr<audio_transport::RealtimeAudioTransport> transport_processor_;
    // ... other members
};
```

### 3. Initialize in Constructor

```cpp
MyVSTPlugin::MyVSTPlugin() {
    transport_processor_ = std::make_unique<audio_transport::RealtimeAudioTransport>(
        44100.0,  // Sample rate (update in setSampleRate)
        100.0,    // Window size in ms (100ms = good quality/latency tradeoff)
        4,        // Hop divisor (4 = 75% overlap, smoother but more CPU)
        2         // FFT multiplier (2 = good frequency resolution)
    );
}
```

### 4. Update Sample Rate

```cpp
void MyVSTPlugin::setSampleRate(double sampleRate) {
    transport_processor_->setSampleRate(sampleRate);
}
```

### 5. Process Audio

```cpp
void MyVSTPlugin::processBlock(AudioBuffer& buffer, AudioBuffer& sidechain) {
    int numSamples = buffer.getNumSamples();

    // Get pointers to audio data
    float* mainInput = buffer.getWritePointer(0);      // Main input (mono)
    float* sidechainInput = sidechain.getReadPointer(0); // Sidechain input
    float* output = buffer.getWritePointer(0);         // Output (overwrites input)

    // Get k parameter from plugin (e.g., from UI slider)
    float k = parameters.getParameter("morph")->getValue(); // 0.0 to 1.0

    // Process the buffer
    transport_processor_->process(
        mainInput,
        sidechainInput,
        output,
        numSamples,
        k
    );
}
```

### 6. Reset on Play/Stop

```cpp
void MyVSTPlugin::reset() {
    transport_processor_->reset();
}
```

### 7. Report Latency

```cpp
int MyVSTPlugin::getLatencySamples() {
    return transport_processor_->getLatencySamples();
}
```

## Parameter Tuning

### Window Size (`window_ms`)

- **Smaller (20-50ms)**: Lower latency, faster response, but poorer frequency resolution (artifacts on low frequencies)
- **Medium (50-100ms)**: Balanced - good for most musical content
- **Larger (100-200ms)**: Best frequency resolution, smoother morphs, but higher latency

### Hop Divisor (`hop_divisor`)

- **2**: 50% overlap - faster, more CPU efficient, but possible artifacts
- **4**: 75% overlap - **recommended** - good balance
- **8**: 87.5% overlap - smoothest, highest CPU usage

### FFT Multiplier (`fft_mult`)

- **1**: No zero-padding - fastest but less smooth spectral interpolation
- **2**: 2x zero-padding - **recommended** - good balance
- **4**: 4x zero-padding - best quality, higher memory usage

## Performance Characteristics

### CPU Usage (44.1kHz, 512-sample buffer)

| Window | Hop Div | FFT Mult | CPU Usage (approx) | Latency |
|--------|---------|----------|-------------------|---------|
| 50ms   | 4       | 2        | ~3-5%             | 25ms    |
| 100ms  | 4       | 2        | ~5-8%             | 50ms    |
| 200ms  | 4       | 2        | ~8-12%            | 100ms   |

### Memory Usage

Approximately:
- Base: ~500 KB
- + (window_size × fft_mult × 8 bytes) for FFT buffers
- Example: 100ms window @ 44.1kHz with 2x padding = ~35 KB

## Algorithm Overview

### CDF-Based Optimal Transport

Unlike the paper's implementation, this uses a simpler approach:

1. **Analysis**: Compute STFT of both inputs
2. **Transport Map**: Treat magnitude spectra as probability distributions
   - Compute CDFs of source and target magnitudes
   - Map each source bin to target: `T(x) = F_Y^{-1}(F_X(x))`
3. **Interpolation**: Move spectral energy along transport paths
   - Each bin moves from position `i` toward target `T[i]`
   - Interpolation factor `k` controls how far (0 = source, 1 = target)
4. **Synthesis**: Inverse STFT with overlap-add

### Differences from Research Implementation

| Feature | Research Version | Real-time Version |
|---------|-----------------|-------------------|
| Analysis | Reassigned spectrogram | Standard STFT |
| Transport | Greedy spectral mass grouping | CDF-based mapping |
| Complexity | O(n log n) | O(n) |
| Quality | Perceptually superior | Good, simpler |
| CPU | Higher | Lower |

## Common Issues & Solutions

### Problem: Crackling/artifacts at low frequencies

**Solution**: Already handled with low-frequency attenuation (< 30 Hz). If still present, increase window size.

### Problem: High CPU usage

**Solutions**:
- Decrease `fft_mult` (try 1 instead of 2)
- Increase `hop_divisor` (try 2 instead of 4)
- Decrease `window_ms` (but this increases artifacts)

### Problem: Latency too high

**Solutions**:
- Decrease `window_ms` (try 50ms)
- Accept the latency and report it correctly via `getLatencySamples()`

### Problem: Morphing sounds too "smeared"

**Solutions**:
- Decrease `window_ms` for better time resolution
- Increase `fft_mult` for smoother spectral interpolation

## Example VST3 Parameter Setup

```cpp
// In your parameter creation code
parameters.addParameter(
    new AudioParameterFloat(
        "morph",
        "Morph Amount",
        0.0f,    // min
        1.0f,    // max
        0.5f     // default (halfway)
    )
);

parameters.addParameter(
    new AudioParameterFloat(
        "window_size",
        "Window Size",
        20.0f,   // min (ms)
        200.0f,  // max (ms)
        100.0f   // default
    )
);
```

## Thread Safety

The current implementation is **not thread-safe** by default. For VST3 plugin use:

1. **Option 1**: Process audio in a single thread (typical VST approach)
2. **Option 2**: Add mutex protection around `process()` calls
3. **Option 3**: Use one instance per processing thread

```cpp
// Option 2 example
std::mutex process_mutex_;

void processBlock(...) {
    std::lock_guard<std::mutex> lock(process_mutex_);
    transport_processor_->process(...);
}
```

## Advanced: Time-Varying k Parameter

You can automate the `k` parameter over time for dynamic morphing:

```cpp
void processBlock(AudioBuffer& buffer, AudioBuffer& sidechain) {
    int numSamples = buffer.getNumSamples();

    // Process sample-by-sample with varying k
    for (int i = 0; i < numSamples; ++i) {
        float k = getAutomatedKValue(i); // Your automation logic

        transport_processor_->process(
            &buffer.getReadPointer(0)[i],
            &sidechain.getReadPointer(0)[i],
            &buffer.getWritePointer(0)[i],
            1,  // Process 1 sample at a time
            k
        );
    }
}
```

**Note**: Processing sample-by-sample is less efficient. For smooth automation, update `k` every 32-64 samples instead.

## Dependencies

- **FFTW3**: Required for FFT operations
- **C++11**: STL containers and features

## License

Same as the main audio_transport library (see LICENSE file).

## References

- Paper: Henderson & Solomon, "Audio Transport: A Generalized Portamento via Optimal Transport" (DAFx 2019)
- Original repository: https://github.com/sportdeath/audio_transport
