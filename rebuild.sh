#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "=== Audio Transport Build Script for macOS ==="

# Check for required dependencies
echo "Checking dependencies..."

if ! brew list fftw &>/dev/null; then
    echo "ERROR: fftw not installed. Run: brew install fftw"
    exit 1
fi

if ! brew list ffmpeg &>/dev/null; then
    echo "ERROR: ffmpeg not installed. Run: brew install ffmpeg"
    exit 1
fi

echo "✓ fftw and ffmpeg found"

# Check if audiorw library exists
if [ ! -f /usr/local/lib/libaudiorw.dylib ] && [ ! -f /opt/homebrew/lib/libaudiorw.dylib ]; then
    echo ""
    echo "=== audiorw library not found, building it ==="

    # audiorw requires ffmpeg 4.x API (incompatible with ffmpeg 5+)
    # ffmpeg@4 is keg-only so it won't affect your system ffmpeg
    if ! brew list ffmpeg@4 &>/dev/null; then
        echo "Installing ffmpeg@4 (keg-only, won't affect your main ffmpeg)..."
        brew install ffmpeg@4
    fi

    FFMPEG4_PREFIX=$(brew --prefix ffmpeg@4)
    echo "Using ffmpeg@4 from: $FFMPEG4_PREFIX"

    # Clone audiorw to a temp location if needed
    AUDIORW_DIR="/tmp/audiorw_build"
    rm -rf "$AUDIORW_DIR"

    echo "Cloning audiorw..."
    git clone https://github.com/sportdeath/audiorw "$AUDIORW_DIR"

    echo "Building audiorw..."
    mkdir -p "$AUDIORW_DIR/build"
    cd "$AUDIORW_DIR/build"

    # Force cmake to use ffmpeg@4 by setting all the FindFFMPEG cache variables
    cmake .. \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DFFMPEG_INCLUDE_DIR="$FFMPEG4_PREFIX/include" \
        -DFFMPEG_avformat_LIBRARY="$FFMPEG4_PREFIX/lib/libavformat.dylib" \
        -DFFMPEG_avcodec_LIBRARY="$FFMPEG4_PREFIX/lib/libavcodec.dylib" \
        -DFFMPEG_avutil_LIBRARY="$FFMPEG4_PREFIX/lib/libavutil.dylib" \
        -DFFMPEG_swscale_LIBRARY="$FFMPEG4_PREFIX/lib/libswscale.dylib" \
        -DFFMPEG_swresample_LIBRARY="$FFMPEG4_PREFIX/lib/libswresample.dylib"
    make

    echo "Installing audiorw (requires sudo)..."
    sudo make install

    cd "$SCRIPT_DIR"
    echo "✓ audiorw installed"
fi

echo ""
echo "=== Building audio_transport ==="

# Clean and recreate build directory
rm -rf build
mkdir -p build
cd build

# Configure with Homebrew paths for fftw
FFTW_PREFIX=$(brew --prefix fftw)
FFMPEG4_PREFIX=$(brew --prefix ffmpeg@4)

# audiorw is installed in /usr/local and expects ffmpeg headers there
# Create symlinks to ffmpeg@4 headers if they don't exist
if [ ! -e /usr/local/include/libavformat ]; then
    echo "Creating ffmpeg@4 symlinks in /usr/local/include..."
    sudo ln -s "$FFMPEG4_PREFIX/include/libavcodec" /usr/local/include/
    sudo ln -s "$FFMPEG4_PREFIX/include/libavdevice" /usr/local/include/
    sudo ln -s "$FFMPEG4_PREFIX/include/libavfilter" /usr/local/include/
    sudo ln -s "$FFMPEG4_PREFIX/include/libavformat" /usr/local/include/
    sudo ln -s "$FFMPEG4_PREFIX/include/libavutil" /usr/local/include/
    sudo ln -s "$FFMPEG4_PREFIX/include/libpostproc" /usr/local/include/
    sudo ln -s "$FFMPEG4_PREFIX/include/libswresample" /usr/local/include/
    sudo ln -s "$FFMPEG4_PREFIX/include/libswscale" /usr/local/include/
fi

cmake .. \
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
    -DBUILD_EXAMPLES=ON \
    -DCMAKE_PREFIX_PATH="$FFTW_PREFIX;/usr/local"

make -j$(sysctl -n hw.ncpu)

echo ""
echo "=== Build complete ==="
echo "Binaries are in: $SCRIPT_DIR/build/"
echo ""
echo "Usage examples:"
echo "  ./build/transport input1.wav input2.wav 20 70 output.wav"
echo "  ./build/glide input.wav 1 output.wav"
