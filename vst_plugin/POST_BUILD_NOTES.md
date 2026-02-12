# Post-Build Notes

## ✅ Build Status: SUCCESS

Your Audio Transport VST3 plugin has been successfully built and installed!

## 📦 Installation Locations

**VST3** (Most DAWs):
```
~/Library/Audio/Plug-Ins/VST3/Audio Transport.vst3
```

**AU** (Logic Pro, GarageBand):
```
~/Library/Audio/Plug-Ins/Components/Audio Transport.component
```

**Standalone** (Run directly):
```
vst_plugin/build/AudioTransportVST_artefacts/Release/Standalone/Audio Transport.app
```

## 🎵 Quick Test

To verify the plugin works:

1. **Open the standalone app** first to test:
   ```bash
   open "build/AudioTransportVST_artefacts/Release/Standalone/Audio Transport.app"
   ```

2. If standalone works, the VST3/AU will also work in your DAW

## ⚠️ Potential Issue: Library Not Found

If your DAW can't load the plugin or you see an error about missing libraries, it's likely because the `audio_transport` library isn't in the system library path.

### Quick Fix Option 1: Install audio_transport Library

```bash
# Go to main library directory
cd /Users/rich/repos/audio_transport/build

# Install to system
sudo make install
```

This installs `libaudio_transport.dylib` to `/usr/local/lib` where the plugin can find it.

### Quick Fix Option 2: Set DYLD_LIBRARY_PATH (Development Only)

For development/testing only (not recommended for production):

```bash
export DYLD_LIBRARY_PATH=/Users/rich/repos/audio_transport/build:$DYLD_LIBRARY_PATH
```

Then launch your DAW from the same terminal.

### Quick Fix Option 3: Bundle Library with Plugin (Production)

For distribution, you'd want to bundle the library inside the plugin. This requires modifying the CMakeLists.txt to:

1. Copy `libaudio_transport.dylib` into the plugin bundle
2. Update rpaths to point to the bundled library
3. Use `install_name_tool` to fix library references

(This is more advanced - see JUCE documentation on bundling dependencies)

## 🧪 Testing Checklist

- [ ] Standalone app launches successfully
- [ ] Plugin appears in DAW plugin list
- [ ] Plugin loads without errors
- [ ] UI displays correctly (morph knob, sliders, bypass button)
- [ ] Audio processes (not silent output)
- [ ] Sidechain input works
- [ ] Morph knob changes sound
- [ ] Window size slider works
- [ ] Bypass button works
- [ ] No crashes during processing

## 🔧 If Plugin Doesn't Appear in DAW

1. **Rescan plugins** in your DAW settings
2. **Restart your DAW** completely
3. **Check plugin format** is enabled (VST3/AU)
4. **Verify installation path** matches your DAW's plugin folders
5. **Check Console app** for error messages (macOS)

## 📊 Build Configuration

**Architecture:** ARM64 (Apple Silicon native)
**FFTW3:** `/opt/homebrew/opt/fftw/lib/libfftw3.3.dylib`
**Audio Transport:** `@rpath/libaudio_transport.dylib`
**JUCE Version:** 8.0.4

## 🎯 Recommended First Test

### Simple Sine Wave Morph

1. Create two tracks with simple sine waves (different frequencies)
2. Insert Audio Transport on Track 1
3. Route Track 2 to sidechain
4. Set morph to 0.0 → should hear Track 1 frequency
5. Set morph to 1.0 → should hear Track 2 frequency
6. Set morph to 0.5 → should hear smooth blend

This tests the core morphing functionality.

## 🐛 Common Issues

### "Plugin failed validation" (Logic Pro)

This is often a signing issue. Try:
```bash
# Re-sign the plugin
codesign --force --deep --sign - ~/Library/Audio/Plug-Ins/Components/Audio\ Transport.component
```

### "Cannot open plugin"

Check if you have the required dependencies:
```bash
# Verify FFTW3
ls -la /opt/homebrew/opt/fftw/lib/libfftw3.3.dylib

# Verify audio_transport
ls -la /Users/rich/repos/audio_transport/build/libaudio_transport.dylib
```

### High CPU usage

- Start with window size at 100ms
- If too high, reduce to 50-80ms
- Check DAW buffer size (increase to 512 or 1024)

### No sound output

- Verify sidechain is actually routed (check DAW routing)
- Try bypass toggle to verify signal path
- Check morph isn't at extreme (try 0.5)
- Verify both inputs have audio

## 📞 Getting Help

- **Documentation**: See `GETTING_STARTED.md` and `README.md` in this directory
- **Technical details**: See parent directory documentation
- **Issues**: https://github.com/sportdeath/audio_transport/issues

## ✨ You're Ready!

The plugin is built and installed. Open your DAW, load the plugin, and start morphing!

For your first session, try the "Vocal Morph" example in `GETTING_STARTED.md`.

**Happy morphing!** 🎵
