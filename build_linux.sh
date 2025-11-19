#!/bin/bash
# Build script for Linux/macOS
# Usage: ./build_linux.sh [Debug|Release]

set -e

BUILD_TYPE=${1:-Release}

echo "Building MeshRepair for Linux/macOS ($BUILD_TYPE)"

# Create build directory
mkdir -p build_linux
cd build_linux

# Configure CMake
# Set CMAKE_PREFIX_PATH if dependencies are not in standard locations
# Add /mnt/e/UBS for nlohmann/json and other header-only libraries
cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DCMAKE_PREFIX_PATH="/mnt/e/DVS;/mnt/e/UBS" \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_INSTALL_PREFIX=INSTALL \
    -DCMAKE_DEBUG_POSTFIX=d \
    ..

if [ $? -ne 0 ]; then
    echo "CMake configuration failed!"
    exit 1
fi

# Build
echo ""
echo "Building $BUILD_TYPE configuration..."
cmake --build . -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

if [ $? -ne 0 ]; then
    echo "Build failed!"
    exit 1
fi

# Install (optional)
# cmake --install .

echo ""
echo "Build completed successfully!"
echo "Executable location: build_linux/meshrepair"
echo ""
