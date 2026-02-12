#!/bin/bash

set -e  # Exit on error

echo "======================================"
echo "Audio Transport VST3 Plugin - Build"
echo "======================================"
echo ""

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Get script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$( cd "$SCRIPT_DIR/.." && pwd )"

# Check if audio_transport library is built
if [ ! -f "$REPO_ROOT/build/libaudio_transport.dylib" ] && [ ! -f "$REPO_ROOT/build/libaudio_transport.so" ]; then
    echo -e "${YELLOW}Audio transport library not found. Building it first...${NC}"
    echo ""

    cd "$REPO_ROOT"
    mkdir -p build
    cd build

    echo "Configuring audio_transport library..."
    cmake ..

    echo "Building audio_transport library..."
    make -j4

    echo -e "${GREEN}✓ Audio transport library built successfully${NC}"
    echo ""
fi

# Build the VST plugin
cd "$SCRIPT_DIR"

# Create build directory
mkdir -p build
cd build

echo "Configuring VST3 plugin..."
echo "This will download JUCE if not already present..."
echo ""

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

echo ""
echo "Building VST3 plugin..."
echo ""

# Build
cmake --build . --config Release -j4

echo ""
echo -e "${GREEN}======================================"
echo "✓ Build Complete!"
echo "======================================${NC}"
echo ""

# Find the VST3 location
if [ "$(uname)" == "Darwin" ]; then
    VST3_PATH="$HOME/Library/Audio/Plug-Ins/VST3/Audio Transport.vst3"
    if [ -d "$VST3_PATH" ]; then
        echo -e "${GREEN}VST3 installed to:${NC}"
        echo "  $VST3_PATH"
    fi
elif [ "$(expr substr $(uname -s) 1 5)" == "Linux" ]; then
    VST3_PATH="$HOME/.vst3/Audio Transport.vst3"
    if [ -d "$VST3_PATH" ]; then
        echo -e "${GREEN}VST3 installed to:${NC}"
        echo "  $VST3_PATH"
    fi
fi

echo ""
echo "Next steps:"
echo "1. Open your DAW"
echo "2. Rescan plugins"
echo "3. Load 'Audio Transport' on a track"
echo "4. Set up sidechain routing"
echo "5. Adjust the morph knob!"
echo ""
echo "See README.md for usage instructions."
echo ""
