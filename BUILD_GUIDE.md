# MeshRepair Build Guide

## Cross-Platform Build Instructions

This project uses portable CMake configuration that preserves important cache variables across platforms.

---

## Windows (Visual Studio)

### Prerequisites
- Visual Studio 2017 or later (tested with VS 2022)
- CMake 3.12+
- CGAL library
- Eigen3 3.2+

### Quick Build

Use the provided build script:

```cmd
build_windows.cmd Release
```

### Manual Build

```cmd
mkdir build && cd build

cmake -G "Visual Studio 17 2022" ^
    -D CMAKE_BUILD_TYPE=Debug,Release ^
    -D CMAKE_MSVC_RUNTIME_LIBRARY="$<$<CONFIG:Debug>:MultiThreadedDebug>$<$<CONFIG:Release>:MultiThreaded>" ^
    -D BUILD_SHARED_LIBS=OFF ^
    -D CMAKE_INSTALL_PREFIX=INSTALL ^
    -D CMAKE_DEBUG_POSTFIX=d ^
    -D CMAKE_PREFIX_PATH=e:/UBS ^
    ..

cmake --build . --config Release -j 8
```

### Output
- **Release**: `build/Release/mesh_hole_filler.exe`
- **Debug**: `build/Debug/mesh_hole_fillerd.exe` (note the 'd' postfix)

---

## Linux / WSL

### Prerequisites
- GCC 7+ or Clang 5+
- CMake 3.12+
- CGAL library
- Eigen3 3.2+

### Quick Build

Use the provided build script:

```bash
chmod +x build_linux.sh
./build_linux.sh Release
```

### Manual Build

```bash
mkdir build && cd build

cmake -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_INSTALL_PREFIX=INSTALL \
    -DCMAKE_DEBUG_POSTFIX=d \
    -DCMAKE_PREFIX_PATH=/mnt/e/UBS \
    ..

cmake --build . -j$(nproc)
```

### Output
- **Release**: `build/mesh_hole_filler`
- **Debug**: `build/mesh_hole_fillerd`

---

## macOS

### Prerequisites
- Xcode Command Line Tools
- CMake 3.12+
- CGAL (via Homebrew: `brew install cgal`)
- Eigen3 (via Homebrew: `brew install eigen`)

### Build

```bash
mkdir build && cd build

cmake -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DCMAKE_INSTALL_PREFIX=INSTALL \
    ..

cmake --build . -j$(sysctl -n hw.ncpu)
```

If using Homebrew dependencies, CMake should auto-detect them. Otherwise, specify:

```bash
cmake -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=/opt/homebrew \
    ..
```

---

## CMake Cache Variables

The following variables are **preserved in CMake cache** and can be set via command line:

| Variable | Description | Default | Example |
|----------|-------------|---------|---------|
| `CMAKE_MSVC_RUNTIME_LIBRARY` | MSVC runtime linking (Windows only) | `MultiThreaded$<$<CONFIG:Debug>:Debug>` | See Windows example |
| `CMAKE_INSTALL_PREFIX` | Installation directory | `build/INSTALL` | `INSTALL`, `/usr/local` |
| `CMAKE_DEBUG_POSTFIX` | Debug library suffix | `d` | `_debug` |
| `CMAKE_PREFIX_PATH` | Dependency search paths | (none) | `e:/DVS`, `/opt/local` |
| `BUILD_SHARED_LIBS` | Build shared/static libraries | `OFF` | `ON` |
| `BUILD_TESTS` | Build test suite | `ON` | `OFF` |
| `ENABLE_OPENMP` | Enable OpenMP parallel processing | `ON` | `OFF` |

---

## Dependency Location

### Auto-Detection

CMake will automatically search for dependencies in:

1. **CMAKE_PREFIX_PATH** (highest priority)
2. Standard system locations
3. Hardcoded fallback paths:
   - CGAL: `/mnt/e/GH/cgal`, `e:/GH/cgal`
   - Eigen3: `/mnt/e/UBS/include/eigen3`, `e:/UBS/include/eigen3`

### Manual Override

If auto-detection fails, specify paths explicitly:

```bash
cmake -DCGAL_DIR=/path/to/cgal \
      -DEIGEN3_INCLUDE_DIR=/path/to/eigen3 \
      ..
```

---

## Build Configurations

### Multi-Config Generators (Visual Studio, Xcode)

Visual Studio and Xcode support multiple configurations in a single build:

```cmd
# Configure once
cmake -G "Visual Studio 17 2022" ..

# Build both configurations
cmake --build . --config Debug
cmake --build . --config Release
```

Outputs:
- `build/Debug/mesh_hole_fillerd.exe`
- `build/Release/mesh_hole_filler.exe`

### Single-Config Generators (Make, Ninja)

Unix Makefiles and Ninja require one build directory per configuration:

```bash
# Release build
mkdir build_release && cd build_release
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

# Debug build (separate directory)
mkdir build_debug && cd build_debug
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build .
```

---

## Build Options

### Static vs Shared Libraries

```bash
# Static (default, recommended for standalone executable)
cmake -DBUILD_SHARED_LIBS=OFF ..

# Shared
cmake -DBUILD_SHARED_LIBS=ON ..
```

### Disable Tests

```bash
cmake -DBUILD_TESTS=OFF ..
```

### Disable OpenMP

```bash
cmake -DENABLE_OPENMP=OFF ..
```

---

## Installation

After building, install to `CMAKE_INSTALL_PREFIX`:

```bash
# Windows
cmake --install . --config Release

# Linux/macOS
cmake --install .
```

This copies `mesh_hole_filler` to `${CMAKE_INSTALL_PREFIX}/bin/`.

---

## Troubleshooting

### CGAL Not Found

```
Error: CGAL not found
```

**Solution**: Set `CGAL_DIR` or `CMAKE_PREFIX_PATH`:

```bash
cmake -DCGAL_DIR=/path/to/cgal ..
# OR
cmake -DCMAKE_PREFIX_PATH=/path/to/dependencies ..
```

### Eigen3 Not Found

```
Error: Eigen3 not found
```

**Solution**: Set `EIGEN3_INCLUDE_DIR`:

```bash
cmake -DEIGEN3_INCLUDE_DIR=/path/to/eigen3 ..
```

### MSVC Runtime Mismatch (Windows)

```
Error: LNK2038: mismatch detected for 'RuntimeLibrary'
```

**Solution**: Ensure all dependencies use the same runtime. Set explicitly:

```cmd
cmake -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>" ..
```

For DLL runtime (dynamic):
```cmd
cmake -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>DLL" ..
```

### Boost Not Found

CGAL requires Boost for some features. Install Boost or set `BOOST_ROOT`:

```bash
cmake -DBOOST_ROOT=/path/to/boost ..
```

### C++17 Not Supported

Update your compiler:
- **Windows**: Visual Studio 2017+
- **Linux**: GCC 7+, Clang 5+
- **macOS**: Xcode 10+

---

## Configuration Summary

After running CMake, you'll see a summary:

```
======================================
  MeshRepair Configuration
======================================
Project version: 1.0.0
Build type: Release
C++ compiler: GNU 11.4.0
C++ standard: C++17

Dependencies:
  CGAL version: 6.0.1
  Eigen3: /mnt/e/UBS/include/eigen3
  Boost: 1.80.0
  OpenMP: TRUE

Options:
  Build tests: ON
  Shared libs: OFF
  Install prefix: /mnt/w/VisualStudio/MeshRepair/build/INSTALL
  Debug postfix: d
  Prefix path: /mnt/e/UBS
======================================
```

Verify all dependencies are found before building.

---

## Quick Reference

| Platform | Command |
|----------|---------|
| **Windows** | `build_windows.cmd Release` |
| **Linux** | `./build_linux.sh Release` |
| **macOS** | `./build_linux.sh Release` |

**Manual**:
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/deps ..
cmake --build . -j8
```

**Run**:
```bash
# Windows
build\Release\mesh_hole_filler.exe input.obj output.obj

# Linux/macOS
build/mesh_hole_filler input.obj output.obj
```

---

## Advanced: Custom Toolchain

For cross-compilation or custom toolchains:

```bash
cmake -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake ..
```

Example toolchain file:
```cmake
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_C_COMPILER gcc-12)
set(CMAKE_CXX_COMPILER g++-12)
```

---

## Developer Notes

### CMake Cache

Important variables are cached in `build/CMakeCache.txt`. To reconfigure:

```bash
# Clear cache and reconfigure
rm -rf build
mkdir build && cd build
cmake ..

# OR modify specific variable
cmake -DCMAKE_PREFIX_PATH=/new/path ..
```

### IDE Integration

**Visual Studio**: Open `build/MeshRepair.sln`

**CLion/VSCode**: Open project root, CMake will auto-configure

**Qt Creator**: File → Open → `CMakeLists.txt`

---

For more information, see [README.md](README.md).
