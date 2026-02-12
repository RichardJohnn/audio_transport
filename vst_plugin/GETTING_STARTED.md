# Getting Started with Audio Transport VST3

## What You're Building

A VST3 plugin that morphs between two audio sources using optimal transport. Think of it as a frequency-aware crossfader that intelligently moves spectral content from one sound to another.

## Prerequisites Checklist

- [ ] macOS 10.13+ or Linux
- [ ] CMake 3.15 or later
- [ ] C++ compiler (Xcode on Mac, GCC/Clang on Linux)
- [ ] FFTW3 library
- [ ] Internet connection (to download JUCE)

### Install FFTW3

**macOS:**
```bash
brew install fftw
```

**Ubuntu/Debian:**
```bash
sudo apt-get install libfftw3-dev
```

## Quick Start (5 Minutes)

### Step 1: Build Everything

```bash
# Navigate to the plugin directory
cd /Users/rich/repos/audio_transport/vst_plugin

# Run the build script
./build.sh
```

This will:
1. Build the audio_transport library (if needed)
2. Download JUCE framework automatically
3. Build the VST3 plugin
4. Install it to your system plugins folder

### Step 2: Verify Installation

**macOS:**
```bash
ls -la ~/Library/Audio/Plug-Ins/VST3/
# You should see: Audio Transport.vst3
```

**Linux:**
```bash
ls -la ~/.vst3/
# You should see: Audio Transport.vst3
```

### Step 3: Test in Your DAW

1. **Open your DAW** (Ableton, Logic, Reaper, etc.)
2. **Rescan plugins** (or restart DAW)
3. **Look for "Audio Transport"** in your effects list
4. **Load it on a track**

## Your First Audio Morph

### Simple Test Setup

Let's create a simple vocal morph:

1. **Create two audio tracks:**
   - Track 1: Your main vocal/audio
   - Track 2: A different vocal/sound to morph into

2. **Insert the plugin:**
   - Add "Audio Transport" to Track 1

3. **Route the sidechain:**
   - Set Track 2 to send to the plugin's sidechain input
   - (Routing varies by DAW - see DAW-specific instructions below)

4. **Adjust the morph:**
   - Morph = 0.0: 100% Track 1
   - Morph = 0.5: 50/50 blend
   - Morph = 1.0: 100% Track 2

5. **Listen to the magic!** ✨

## DAW-Specific Instructions

### Ableton Live

1. Insert Audio Transport on Track A
2. In the plugin title bar, click the sidechain triangle
3. Select "Audio From" → choose Track B
4. Play both tracks - adjust morph knob

### Logic Pro X

1. Insert Audio Transport on Track A as a stereo effect
2. Click the "Side Chain" menu in the plugin
3. Select the bus or track you want to morph with
4. Adjust morph knob

### Reaper

1. Insert Audio Transport on Track A
2. Select Track B → right-click → "Route"
3. Add new send to Track A
4. Set send to pins 3/4 (sidechain input)
5. Adjust morph knob

### FL Studio

1. Insert Audio Transport on Mixer Track A
2. Route another track to the sidechain input
3. Right-click the plugin → "Sidechain to this track"
4. Select source track

## Understanding the Parameters

### Morph (Main Control)

**What it does:** Blends between main input and sidechain

```
0.0 ────────── 0.5 ────────── 1.0
Main     50/50 Blend    Sidechain
```

**Creative uses:**
- Static blend: Find sweet spot (try 0.3-0.7)
- Automation: Smooth transitions
- MIDI control: Real-time performance

### Window Size (Quality/Latency Tradeoff)

**What it does:** Controls the analysis window size

```
20ms ──────── 100ms ──────── 200ms
Low Latency   Balanced    High Quality
Less Smooth              More Smooth
```

**Recommendations:**
- **Drums/percussion:** 50-80ms (tight transients)
- **Vocals:** 80-120ms (good clarity)
- **Pads/ambient:** 120-200ms (smooth morphing)
- **Live performance:** 50-80ms (low latency)

### Bypass

Standard bypass - compare processed vs. original

## Example Projects

### 1. Vocal Transformer

**Goal:** Morph between two different vocal takes

**Setup:**
- Track 1: Main vocal (verse)
- Track 2: Harmony/different take
- Morph: Automate from 0 to 1 over 2 bars

**Settings:**
- Window Size: 100ms
- Morph automation: Smooth curve

**Result:** Seamless vocal transformation

### 2. Instrument Hybrid

**Goal:** Create hybrid piano sound

**Setup:**
- Track 1: Acoustic piano
- Track 2: Electric piano
- Morph: 0.4-0.6 (blend both)

**Settings:**
- Window Size: 120ms
- Morph: Static at 0.5

**Result:** Unique hybrid timbre

### 3. Rhythmic Morphing

**Goal:** Pulsing morph effect

**Setup:**
- Track 1: Synth pad
- Track 2: Rhythmic element (drums/perc)
- Morph: LFO automation (0→1→0)

**Settings:**
- Window Size: 80ms
- Morph: Fast automation (1/4 note rate)

**Result:** Rhythmic texture modulation

### 4. Sound Design

**Goal:** Evolving soundscape

**Setup:**
- Track 1: Ambient drone
- Track 2: Textural noise
- Morph: Slow automation over 8 bars

**Settings:**
- Window Size: 180ms (very smooth)
- Morph: 0→1 very slow

**Result:** Evolving, organic texture

## Troubleshooting

### Build Issues

**Error: "Cannot find JUCE"**
```bash
# CMake should download JUCE automatically
# If it fails, check internet connection
# Or download manually from: https://github.com/juce-framework/JUCE
```

**Error: "Cannot find fftw3"**
```bash
# Install FFTW3
brew install fftw  # macOS
sudo apt-get install libfftw3-dev  # Linux
```

**Error: "Cannot find audio_transport"**
```bash
# Build the library first
cd /Users/rich/repos/audio_transport/build
cmake .. && make
```

### Plugin Issues

**Plugin doesn't appear in DAW**
- Rescan plugins in DAW preferences
- Check installation path (see Step 2 above)
- Restart DAW
- Check VST3 format is enabled in DAW

**No sidechain input available**
- Some DAWs require sidechain enabled in track settings
- Check DAW documentation for sidechain routing
- Try inserting as "mono + sidechain" format

**High CPU usage**
- Reduce window size to 50-70ms
- Increase DAW buffer size
- Disable plugin when not in use

**Artifacts or glitches**
- Increase window size to 120-150ms
- Check both inputs have audio
- Ensure sample rates match

### Audio Issues

**Output sounds weird**
- Check morph value (0.5 is 50/50)
- Verify sidechain is routed correctly
- Try different window sizes
- Compare with bypass enabled

**Too much latency**
- Reduce window size to 50-60ms
- Enable delay compensation in DAW
- Check total plugin latency in DAW

**No output at all**
- Check bypass is disabled
- Verify both inputs have signal
- Check sidechain routing
- Try morph at 0.0 (should pass main input)

## Advanced Tips

### Automation Ideas

1. **Slow Morph:** Automate 0→1 over 8-16 bars for transitions
2. **Pulsing:** LFO automation synced to tempo
3. **Performance:** Map to MIDI controller for live control
4. **Step Sequencer:** Create rhythmic morph patterns

### Processing Chains

**Before Audio Transport:**
- EQ (shape sources before morphing)
- Compression (control dynamics)
- Saturation (add harmonics)

**After Audio Transport:**
- Reverb (blend morphed output)
- Delay (create space)
- Final EQ (polish blend)

### Creative Routing

**Parallel Processing:**
- Send dry signal to bus
- Process with Audio Transport
- Blend dry/wet for subtle effects

**Multi-Stage Morphing:**
- Chain multiple instances
- Each morphs different aspects
- Complex transformations

**Frequency-Split Morphing:**
- Split into bands (low/mid/high)
- Different morph amounts per band
- Recombine for targeted morphing

## Performance Tips

### CPU Optimization

1. **Freeze tracks** when morph is static
2. **Use bypass** when not needed
3. **Reduce window size** on less critical tracks
4. **Increase buffer size** in DAW for smoother performance

### Quality vs. Speed

| Use Case | Window Size | CPU | Latency | Quality |
|----------|-------------|-----|---------|---------|
| Live performance | 50ms | Low | 25ms | Good |
| Mixing | 100ms | Med | 50ms | Great |
| Mastering | 150ms | High | 75ms | Excellent |
| Sound design | 200ms | High | 100ms | Maximum |

## Next Steps

1. ✅ **Build the plugin** (done if you followed Quick Start)
2. 🎵 **Try the example projects** above
3. 🎛️ **Experiment with parameters** on your own material
4. 📖 **Read the main README** for technical details
5. 🎨 **Get creative!** This plugin enables unique sounds impossible with normal crossfading

## Getting Help

- **Documentation:** See `README.md` in this directory
- **Technical details:** See `REALTIME_VST3_GUIDE.md` in parent directory
- **Issues:** https://github.com/sportdeath/audio_transport/issues
- **Algorithm paper:** Henderson & Solomon, DAFx 2019

## What Makes This Special?

Unlike a normal crossfader that just blends amplitude, Audio Transport:

✨ **Tracks frequency content** and morphs it intelligently
✨ **Preserves harmonic structure** during transitions
✨ **Creates smooth spectral motion** between sounds
✨ **Enables impossible hybrids** that sound coherent

Try it on:
- Vocals ↔ Synths
- Acoustic ↔ Electronic
- Dry ↔ Wet
- Any two sounds you can imagine!

---

**Ready to morph?** Run `./build.sh` and start creating! 🚀
