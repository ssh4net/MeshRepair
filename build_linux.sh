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
# Adjust CMAKE_PREFIX_PATH to your dependency location
cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_INSTALL_PREFIX=INSTALL \
    -DCMAKE_DEBUG_POSTFIX=d \
    -DCMAKE_PREFIX_PATH=/mnt/e/UBS \
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
echo "Executable location: build_linux/mesh_hole_filler"
echo ""
