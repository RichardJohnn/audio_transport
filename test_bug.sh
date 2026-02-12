#!/bin/bash
# Test case for sample-rate mismatch bug
# This reproduces the stuck-at-max audio corruption issue

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Check if test files exist
if [ ! -f "../audio_transport_web_ui/80s.wav" ] || [ ! -f "../audio_transport_web_ui/100.wav" ]; then
    echo "ERROR: Test files not found. Expected:"
    echo "  ../audio_transport_web_ui/80s.wav (48kHz)"
    echo "  ../audio_transport_web_ui/100.wav (44.1kHz)"
    exit 1
fi

# Build if needed
if [ ! -f "build/transport" ]; then
    echo "Building transport..."
    mkdir -p build
    cd build
    cmake .. -D BUILD_EXAMPLES=ON
    make -j$(nproc)
    cd ..
fi

# Run the test
echo "Running transport with mismatched sample rates..."
echo "  Left:  80s.wav (48kHz)"
echo "  Right: 100.wav (44.1kHz)"
echo ""

./build/transport \
    ../audio_transport_web_ui/80s.wav \
    ../audio_transport_web_ui/100.wav \
    0 100 \
    /tmp/test_output.wav

echo ""
echo "Output written to /tmp/test_output.wav"
echo ""

# Analyze output for stuck regions
echo "Analyzing output for stuck-at-max corruption..."
python3 << 'EOF'
import wave
import struct

try:
    with wave.open('/tmp/test_output.wav', 'rb') as wav:
        sample_rate = wav.getframerate()
        n_frames = wav.getnframes()
        n_channels = wav.getnchannels()
        frames = wav.readframes(n_frames)

    samples = struct.unpack(f'<{n_frames * n_channels}h', frames)
    left = samples[0::n_channels]

    # Count stuck samples
    stuck_max = sum(1 for x in left if x == 32767)
    stuck_min = sum(1 for x in left if x == -32768)
    total = len(left)

    print(f"Total samples: {total}")
    print(f"Stuck at +32767: {stuck_max} ({100*stuck_max/total:.2f}%)")
    print(f"Stuck at -32768: {stuck_min} ({100*stuck_min/total:.2f}%)")

    if stuck_max > 1000 or stuck_min > 1000:
        print("")
        print("BUG REPRODUCED: Significant stuck-at-max/min corruption detected!")
        exit(1)
    else:
        print("")
        print("OK: No significant corruption detected.")
        exit(0)

except Exception as e:
    print(f"Error analyzing output: {e}")
    exit(1)
EOF
