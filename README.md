# Mesh Repair

A cross-platform CLI tool for filling holes in triangle meshes using CGAL's implementation of the Liepa 2003 algorithm with Laplacian (harmonic) fairing.

## Features

- **Robust Hole Filling**: Uses constrained Delaunay triangulation (2D/3D) with automatic fallback
- **Laplacian Fairing**: Smooth blending with configurable continuity (C⁰, C¹, C²)
- **Partitioned Parallel Filling**: Default mode balances work across threads, capping partitions to hole count and edge budget
- **Global Hole Size Guards**: Max-diameter checks use the full mesh bounding box (partition-safe) to avoid over-skipping large holes
- **Tunable Workload**: Minimum-edges threshold per partition (`--min-edges`) to avoid oversharding tiny holes
- **Multi-Format Support**: OBJ, PLY, OFF formats
- **Scalable**: Optimized for meshes with millions of polygons
- **Cross-Platform**: Windows, Linux, macOS

## Algorithm

Based on:
- **Liepa 2003**: "Filling Holes in Meshes" - Eurographics Symposium on Geometry Processing
- **Botsch et al. 2008**: "On Linear Variational Surface Deformation Methods" - IEEE TVCG

### Process

1. **Triangulation**: 2D CDT on best-fit plane, with 3D Delaunay fallback
2. **Refinement**: Match local mesh density
3. **Fairing**: Bi-Laplacian smoothing (Δ²f = 0) with boundary constraints

## Building

### Prerequisites

- C++17 compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.12+
- CGAL (located at `/mnt/e/GH/cgal/` or set `CGAL_DIR`)
- Eigen3 3.2+ (located at `/mnt/e/UBS/include/eigen3/` or set `EIGEN3_INCLUDE_DIR`)

### Dependencies

- **Required**: CGAL, Eigen3
- **Engine IPC**: nlohmann/json (fetched automatically if missing)
- **Fast OBJ loading**: RapidOBJ (header-only, optional; falls back to CGAL loader)
- **Logging**: spdlog (optional; falls back to std::ostream logger)

### Build Steps

```bash
# Clone or extract project
cd MeshRepair

# Create build directory
mkdir build && cd build

# Configure
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build
cmake --build . -j$(nproc)

# Install (optional)
cmake --install . --prefix /usr/local
```

### Platform-Specific

**Linux/WSL:**
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc)
```

**Windows (Visual Studio):**
```cmd
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

**macOS:**
```bash
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(sysctl -n hw.ncpu)
```

## Usage

### Basic

```bash
./mesh_hole_filler input.obj output.obj
```

### With Options

```bash
./mesh_hole_filler input.ply output.ply -v 2 --validate
```

### Advanced

```bash
./mesh_hole_filler mesh.obj repaired.obj \
    --continuity 2 \
    --max-boundary 500 \
    --max-diameter 0.05 \
    --validate
```

`--max-boundary` limits boundary vertex count. `--max-diameter` caps hole diameter relative to the *full mesh* bounding-box diagonal (cached before partitioning so all threads use the same reference). Use values >1.0 for openings larger than the overall mesh diagonal.

Engine/Blender integration uses the same guards (`max_boundary`, `max_diameter`) and the cached full-mesh diagonal so partitioned fills behave consistently across the CLI, engine API, and Blender addon.

## Command-Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--continuity <0\|1\|2>` | Fairing continuity (C⁰/C¹/C²) | 1 |
| `--max-boundary <n>` | Max hole boundary vertices | 1000 |
| `--max-diameter <r>` | Max hole diameter ratio | 0.1 |
| `--no-2d-cdt` | Disable 2D constrained Delaunay | enabled |
| `--no-3d-delaunay` | Disable 3D Delaunay fallback | enabled |
| `--skip-cubic` | Skip cubic search | disabled |
| `--no-refine` | Disable mesh refinement | enabled |
| `--holes_only` | Output only reconstructed faces (partitioned mode) | off |
| `-v, --verbose <0-4>` | Verbosity level (0=quiet, 4=debug dumps) | 1 |
| `--validate` | Validate mesh before/after | off |
| `--ascii-ply` | Save PLY files in ASCII format | off (binary) |
| `--threads <n>` | Worker threads (0 = auto) | hw_cores/2 |
| `--queue-size <n>` | Pipeline queue size (legacy mode) | 10 |
| `--min-edges <n>` | Minimum boundary edges to justify a partition | 100 |
| `--no-partition` | Use legacy (single-mesh) pipeline | partitioned |
| `--cgal-loader` | Force CGAL OBJ loader | RapidOBJ/auto |
| `--temp-dir <path>` | Directory for debug/intermediate dumps (verbosity 4) | system temp |

### Preprocessing Options

| Option | Description | Default |
|--------|-------------|---------|
| `--preprocess / --no-preprocess` | Enable or disable preprocessing pipeline | on |
| `--no-remove-duplicates` | Disable duplicate vertex removal | enabled |
| `--no-remove-non-manifold` | Disable non-manifold vertex removal | enabled |
| `--remove-long-edges <r>` | Remove polygons with any edge longer than `r` × mesh bbox diagonal (disabled by default) | off |
| `--no-remove-isolated` | Disable isolated vertex cleanup | enabled |
| `--no-remove-small` | Disable small component removal | enabled |
| `--non-manifold-passes <n>` | Number of non-manifold removal passes | 10 |
| Verbosity 4 | Dump intermediate meshes as binary PLY | off |

## Performance

| Mesh Size | Holes | Avg Boundary | Expected Time |
|-----------|-------|--------------|---------------|
| 100K tris | 10 | 50 verts | < 1 sec |
| 1M tris | 50 | 100 verts | 2-5 sec |
| 10M tris | 100 | 200 verts | 10-30 sec |
| 50M tris | 500 | 100 verts | 1-3 min |

**Note**: Performance depends on hole boundary size, not total mesh size.
The partitioned filler runs by default; partitions are capped by hole count and an edge budget (`--min-edges`, default 100) to avoid oversharding small holes. Adjust `--threads` and `--min-edges` together for best throughput.

## Examples

### Fill all holes with default settings
```bash
./mesh_hole_filler damaged.obj repaired.obj
```

### High-quality smooth filling (C² continuity)
```bash
./mesh_hole_filler input.ply output.ply --continuity 2
```

### Fast filling for large models
```bash
./mesh_hole_filler large.obj fixed.obj --skip-cubic --no-refine
```

### Detailed analysis
```bash
./mesh_hole_filler mesh.obj result.obj -v 2 --validate
```

### Preprocessing for damaged meshes
```bash
./mesh_hole_filler damaged.obj repaired.obj --preprocess --verbose
```

### Preprocessing with custom passes
```bash
./mesh_hole_filler noisy.ply clean.ply --preprocess --non-manifold-passes 3
```

### Debug mode (dump intermediate meshes)
```bash
./mesh_hole_filler input.obj output.obj --preprocess --debug --verbose
```
This creates:
- `debug_duplicates.ply` - After duplicate vertex removal
- `debug_nonmanifold.ply` - After non-manifold vertex removal
- `debug_isolated.ply` - After isolated vertex removal
- `debug_largest_component.ply` - After small component removal

## Output Example

```
=== MeshHoleFiller v1.0.0 ===

Loading mesh from: input.obj
  Vertices: 12483
  Faces: 24862
  Edges: 37345

Detecting holes...
Detected 3 hole(s):
  Hole 1: 47 boundary vertices, diameter ≈ 2.341
  Hole 2: 23 boundary vertices, diameter ≈ 1.102
  Hole 3: 65 boundary vertices, diameter ≈ 3.876

Filling 3 hole(s)...
  Hole 1/3 (47 boundary vertices):
    Filled: 89 faces, 42 vertices added
  Hole 2/3 (23 boundary vertices):
    Filled: 41 faces, 18 vertices added
  Hole 3/3 (65 boundary vertices):
    Filled: 127 faces, 62 vertices added

=== Hole Filling Summary ===
  Filled successfully: 3
  Failed: 0
  Skipped (too large): 0
  Faces added: 257
  Vertices added: 122
  Total time: 847.3 ms

Saving result to: output.obj
  Vertices: 12605
  Faces: 25119

Done! Successfully processed mesh.
```

## Troubleshooting

### Fairing Fails
- Increase boundary constraints (use smaller `--continuity` value)
- Check that hole boundary is valid and manifold

### Holes Too Large
- Adjust `--max-boundary` or `--max-diameter`
- Split large holes manually

### Build Fails
- Verify Eigen3 3.2+ is installed
- Check that CGAL_DIR and EIGEN3_INCLUDE_DIR are set correctly
- Ensure C++17 support

## License

This project uses CGAL, which is licensed under GPL-3.0 or commercial license.
Your project inherits the GPL-3.0 license unless you obtain a commercial CGAL license.

## References

1. Peter Liepa. "Filling Holes in Meshes." *Eurographics Symposium on Geometry Processing*, 2003.
2. Mario Botsch et al. "On Linear Variational Surface Deformation Methods." *IEEE TVCG*, 2008.
3. CGAL Documentation: [Polygon Mesh Processing](https://doc.cgal.org/latest/Polygon_mesh_processing/)

## Project Structure

```
MeshRepair/
├── CMakeLists.txt          # Root build configuration
├── README.md               # This file
├── cmake/                  # CMake helper modules
│   ├── FindCGAL.cmake
│   └── FindEigen3.cmake
├── meshrepair/             # Main source directory
│   ├── include/            # Public headers
│   │   ├── types.h
│   │   ├── config.h
│   │   ├── mesh_loader.h
│   │   ├── hole_detector.h
│   │   ├── hole_filler.h
│   │   ├── mesh_validator.h
│   │   ├── mesh_preprocessor.h
│   │   └── progress_reporter.h
│   ├── main.cpp
│   ├── mesh_loader.cpp
│   ├── hole_detector.cpp
│   ├── hole_filler.cpp
│   ├── mesh_validator.cpp
│   ├── mesh_preprocessor.cpp
│   └── progress_reporter.cpp
├── tests/                  # Test suite
│   ├── CMakeLists.txt
│   └── test_data/
└── docs/                   # Additional documentation
    ├── USAGE.md
    └── ALGORITHM.md
```

## Contributing

Contributions welcome! Please ensure:
- Code follows existing style
- All tests pass
- Documentation is updated

## Contact

For issues and questions, please open an issue on the project repository.
