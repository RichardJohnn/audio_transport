# Audio Transport VST3 - Quick Reference Card

## Build Command
```bash
cd /Users/rich/repos/audio_transport/vst_plugin
./build.sh
```

## Plugin Location After Build
- **macOS:** `~/Library/Audio/Plug-Ins/VST3/Audio Transport.vst3`
- **Linux:** `~/.vst3/Audio Transport.vst3`

## Parameters

| Parameter | Range | Default | Purpose |
|-----------|-------|---------|---------|
| **Morph** | 0.0 - 1.0 | 0.5 | Blend amount (0=main, 1=sidechain) |
| **Window Size** | 20-200 ms | 100 ms | Quality vs latency |
| **Bypass** | On/Off | Off | True bypass |

## Preset Recommendations

### Live Performance
- Window Size: **50ms**
- Latency: 25ms
- CPU: Low

### Studio Mixing
- Window Size: **100ms**
- Latency: 50ms
- CPU: Medium

### Sound Design
- Window Size: **150-200ms**
- Latency: 75-100ms
- CPU: Higher

## Quick Troubleshooting

| Problem | Fix |
|---------|-----|
| Plugin not showing | Rescan plugins in DAW |
| No sidechain option | Check DAW sidechain routing |
| Too much CPU | Lower window size |
| Artifacts/glitches | Raise window size |
| Build fails | `brew install fftw` |

## Sidechain Routing (Quick)

**Ableton:** Plugin → Sidechain triangle → Audio From → [Track]
**Logic:** Plugin → Side Chain menu → [Bus/Track]
**Reaper:** Route track to pins 3/4 of plugin track

## Example Settings

### Vocal Morph
```
Main: Lead vocal
Sidechain: Harmony
Morph: 0.4
Window: 100ms
```

### Instrument Hybrid
```
Main: Acoustic piano
Sidechain: Electric piano
Morph: 0.6
Window: 120ms
```

### Transition Effect
```
Main: Intro sound
Sidechain: Main sound
Morph: Automate 0→1 (8 bars)
Window: 150ms
```

## Latency Reference

| Window | Latency |
|--------|---------|
| 50ms   | 25ms    |
| 100ms  | 50ms    |
| 150ms  | 75ms    |
| 200ms  | 100ms   |

## Morph Value Guide

```
0.0     Main input only (100% main)
0.25    Mostly main (75% main, 25% sidechain)
0.5     Even blend (50/50)
0.75    Mostly sidechain (25% main, 75% sidechain)
1.0     Sidechain only (100% sidechain)
```

## Files You Need

**Must Read:**
- `GETTING_STARTED.md` - Start here!
- `README.md` - Full documentation

**Reference:**
- `QUICK_REFERENCE.md` - This file
- Parent directory guides - Technical details

## Support

- Docs: See `.md` files in this directory
- Issues: https://github.com/sportdeath/audio_transport/issues
- Paper: Henderson & Solomon, DAFx 2019

---

**Save this file - it has everything you need at a glance!**
