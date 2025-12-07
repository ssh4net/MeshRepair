# MeshRepair CLI - User Guide

> Command-line tool for automatic mesh hole detection and filling

MeshRepair CLI provides batch processing capabilities for detecting and filling holes in triangulated 3D meshes. This guide covers installation, usage, and configuration options.

![MeshRepair CLI Banner](https://ssh4net.github.io/MeshRepair/images/cli-banner.gif)

---

## Table of Contents

- [Quick Start](#quick-start)
- [Mesh Hole Concepts](#mesh-hole-concepts)
- [Processing Pipeline](#processing-pipeline)
- [Installation](#installation)
- [Basic Usage](#basic-usage)
- [Continuity Modes](#continuity-modes)
- [Configuration Options](#configuration-options)
- [3D Scan Processing](#3d-scan-processing)
- [File Format Support](#file-format-support)
- [Troubleshooting](#troubleshooting)
- [Command Reference](#command-reference)

---

## Quick Start

Basic mesh repair with default settings:

```bash
meshrepair input.obj output.obj
```

This command will:
1. Load the input mesh
2. Preprocess to fix topology issues
3. Remove small disconnected components
4. Detect all holes
5. Fill holes with C¹ continuity
6. Save the result

---

## Mesh Hole Concepts

### Definition

A mesh hole is a boundary loop where the surface is discontinuous. Holes are defined by a sequence of border edges that form a closed loop without adjacent faces on one side.

### Common Sources

| Source | Characteristics |
|--------|-----------------|
| **3D Scanning** | Irregular boundaries from occlusion or limited scanner coverage |
| **Boolean Operations** | Clean geometric openings from CSG operations |
| **Incomplete Modeling** | Intentionally or accidentally omitted faces |
| **File Conversion** | Data loss during format translation |
| **Data Corruption** | Random missing faces from file damage |

### Applications Requiring Watertight Meshes

- **Additive Manufacturing**: Slicing software requires closed surfaces
- **Game Engines**: Physics and rendering systems expect manifold geometry
- **Simulation**: CFD and FEA solvers need closed boundary conditions
- **Rendering**: Ray tracing and global illumination require consistent normals

---

## Processing Pipeline

MeshRepair processes meshes through four sequential stages:

### Stage 1: Preprocessing

Topology cleanup operations:

| Operation | Description |
|-----------|-------------|
| **Duplicate Merging** | Combines vertices at identical positions |
| **Non-Manifold Removal** | Removes edges with >2 adjacent faces and vertices with non-disk neighborhoods |
| **3-Face Fan Collapse** | Simplifies degenerate vertex configurations |
| **Isolated Vertex Removal** | Deletes vertices with no face connections |
| **Small Component Removal** | Removes disconnected mesh fragments below threshold |

### Stage 2: Hole Detection

Identifies all boundary loops by traversing border halfedges:

For each detected hole:
- **Boundary vertex count**: Number of vertices forming the hole perimeter
- **Estimated diameter**: Bounding box diagonal of boundary vertices
- **Boundary halfedge**: Entry point for filling algorithm

### Stage 3: Hole Filling

Each hole is triangulated using the Liepa algorithm:

1. **Triangulation**: Constrained Delaunay triangulation (2D projection or 3D)
2. **Refinement**: Optional vertex insertion to match local mesh density
3. **Fairing**: Laplacian smoothing for surface continuity (C⁰/C¹/C²)

### Stage 4: Output

The repaired mesh is written to the specified output file.

### Parallel Partitioned Filling (Default)

The CLI uses partitioned parallel filling by default (`--no-partition` switches to the legacy single-mesh pipeline). Holes are grouped into partitions and filled in parallel; results are merged automatically. Partition count is capped by the number of holes and a minimum edge budget (`--min-edges`) to avoid oversharding tiny holes. The `--holes_only` flag is supported only in partitioned mode.

---

## Installation

### Windows

1. Download `meshrepair-windows.zip` from the [Releases page](https://github.com/your-repo/meshrepair/releases)
2. Extract to desired location (e.g., `C:\Tools\MeshRepair\`)
3. Optionally add to system PATH:
   ```cmd
   setx PATH "%PATH%;C:\Tools\MeshRepair"
   ```

### Linux

```bash
wget https://github.com/your-repo/meshrepair/releases/latest/meshrepair-linux.tar.gz
tar -xzf meshrepair-linux.tar.gz
sudo mv meshrepair /usr/local/bin/
```

### macOS

```bash
wget https://github.com/your-repo/meshrepair/releases/latest/meshrepair-macos.tar.gz
tar -xzf meshrepair-macos.tar.gz
sudo mv meshrepair /usr/local/bin/
```

### Verification

```bash
meshrepair --help
```

---

## Basic Usage

### Standard Repair

```bash
meshrepair model.obj repaired.obj
```

### Verbose Output

```bash
meshrepair model.obj repaired.obj -v 2
```

Example output:
```
[INFO] Loading mesh: model.obj
[INFO] Vertices: 45,230  Faces: 89,456
[INFO] Preprocessing mesh...
  - Duplicates merged: 12
  - Non-manifold removed: 3
[INFO] Detecting holes...
  - Found 4 holes
[INFO] Filling holes...
  - Hole 1/4: 23 boundary vertices - filled (45 faces added)
  - Hole 2/4: 156 boundary vertices - filled (312 faces added)
  - Hole 3/4: 8 boundary vertices - filled (12 faces added)
  - Hole 4/4: 67 boundary vertices - filled (134 faces added)
[INFO] Saving: repaired.obj
[INFO] Complete. Time: 2.34s
```

### Format Conversion

```bash
# OBJ to PLY (binary)
meshrepair model.obj model.ply

# PLY to OBJ
meshrepair scan.ply scan.obj

# PLY binary to ASCII
meshrepair model.ply output.ply --ascii-ply
```

---

## Continuity Modes

The `--continuity` parameter controls surface smoothness at filled regions:

### C⁰ Continuity (Positional)

```bash
meshrepair model.obj fixed.obj --continuity 0
```

- Filled surface meets boundary positions only
- Visible seam at patch boundary
- Fastest computation

### C¹ Continuity (Tangent) - Default

```bash
meshrepair model.obj fixed.obj --continuity 1
```

- Filled surface matches boundary tangent directions
- Smooth visual transition
- Balanced computation time

### C² Continuity (Curvature)

```bash
meshrepair model.obj fixed.obj --continuity 2
```

- Filled surface matches boundary curvature
- Minimal visible seam
- Highest computation cost

---

## Configuration Options

### Hole Size Limits

Control which holes are processed based on size:

```bash
# Maximum boundary vertices (skip larger holes)
meshrepair model.obj fixed.obj --max-boundary 500

# Maximum diameter as ratio of mesh bounding box
meshrepair model.obj fixed.obj --max-diameter 0.05
```

Notes:
- `--max-diameter` is measured against the **full mesh** bounding-box diagonal; the value is cached before partitioning so submeshes use the same reference.
- For very large openings, pass values >1.0 to allow holes larger than the overall mesh diagonal.
- Verbosity ≥2 will log when a hole is skipped because it exceeded either guard.

### Preprocessing Control

```bash
# Skip all preprocessing
meshrepair model.obj fixed.obj --no-preprocess

# Selective preprocessing
meshrepair model.obj fixed.obj --no-remove-non-manifold --no-remove-3facefan

# Remove polygons with long edges (relative to bbox)
meshrepair model.obj fixed.obj --remove-long-edges 0.25
```

`--remove-long-edges <r>` removes any polygon whose edge length exceeds `r` times the mesh bounding-box diagonal. The diagonal is computed once on the full mesh before partitioning so all threads and partitions use the same reference. Use small ratios (e.g. 0.1–0.3) to aggressively prune stray “spike” triangles or extremely stretched polygons; leave this option off for CAD-like meshes where long edges are intentional.

### Algorithm Selection

```bash
# Disable 2D triangulation (use 3D only)
meshrepair model.obj fixed.obj --no-2d-cdt

# Disable mesh refinement
meshrepair model.obj fixed.obj --no-refine

# Skip cubic search algorithm (faster, may reduce quality)
meshrepair model.obj fixed.obj --skip-cubic

# Legacy (non-partitioned) pipeline
meshrepair model.obj fixed.obj --no-partition

# Holes-only output (partitioned mode only)
meshrepair model.obj fixed.obj --holes_only
```

### Threading

```bash
# Specify thread count
meshrepair large_model.obj fixed.obj --threads 4

# Auto-detect (default)
meshrepair large_model.obj fixed.obj --threads 0
```

### Debug Output

```bash
meshrepair problem.obj fixed.obj --temp-dir ./debug -v 4
```

Generated files:
- `debug_00_original_loaded.ply` - Input mesh after loading
- `debug_06_partition_*.ply` - Partitioned submeshes
- `debug_07_partition_*_filled.ply` - Submeshes after filling
- `debug_08_final_merged.ply` - Final merged result

Notes for partitioned mode:
- Partitions are greedily load-balanced by hole boundary size, so large holes are spread across threads.
- Partition count is capped by hole count and a minimum edge budget (`--min-edges`) to avoid overhead on tiny holes.
- Filled submesh dumps (`debug_07*`) now reflect the holes actually patched inside each partition.
- `--holes_only` is honored only when partitioned mode is enabled (default).

---

## 3D Scan Processing

3D scans present specific challenges for hole filling.

### Common Issues

1. **Occlusion gaps**: Areas not visible to scanner
2. **Edge artifacts**: Incomplete boundaries at scan limits
3. **Noise fragments**: Small disconnected components
4. **Missing fine detail**: Gaps in hair, fingers, thin features

### Recommended Settings

```bash
# Heavy preprocessing for scan artifacts
meshrepair raw_scan.ply cleaned.ply -v 2

# High-quality filling with larger size limits
meshrepair cleaned.ply final.ply --continuity 2 --max-boundary 2000 --max-diameter 0.2
```

### Large Scan Files

For multi-million polygon scans:

```bash
# Use all available threads
meshrepair large_scan.ply repaired.ply --threads 0

# Binary PLY for faster I/O
meshrepair large_scan.ply repaired.ply
```

### Preserving Detail

```bash
# Maximum quality settings
meshrepair artifact.ply restored.ply \
    --continuity 2 \
    --max-diameter 0.15 \
    -v 2
```

---

## File Format Support

| Format | Extension | Read | Write | Notes |
|--------|-----------|------|-------|-------|
| **Wavefront OBJ** | `.obj` | Yes | Yes | ASCII format, wide compatibility |
| **Stanford PLY** | `.ply` | Yes | Yes | Binary (default) or ASCII |
| **Object File Format** | `.off` | Yes | Yes | CGAL native format |

### Format Selection

- **OBJ**: Maximum compatibility with other software
- **PLY Binary**: Fastest I/O for large files (default for PLY output)
- **PLY ASCII**: Human-readable, use `--ascii-ply` flag

```bash
# Force ASCII PLY output
meshrepair model.obj output.ply --ascii-ply
```

---

## Troubleshooting

### No Holes Detected

Possible causes:
- Mesh is already watertight
- All holes exceed size limits

Solutions:
```bash
# Check with verbose output
meshrepair model.obj fixed.obj -v 2

# Increase size limits
meshrepair model.obj fixed.obj --max-boundary 5000 --max-diameter 0.5
```

### Hole Filling Failures

Possible causes:
- Degenerate geometry (zero-area triangles)
- Self-intersecting boundaries
- Complex non-planar hole shapes

Solutions:
```bash
# Try different triangulation
meshrepair model.obj fixed.obj --no-2d-cdt

# Enable all preprocessing
meshrepair model.obj fixed.obj -v 2
```

### Performance Issues

Solutions:
```bash
# Reduce quality for faster processing
meshrepair large.obj fixed.obj --continuity 0 --skip-cubic --no-refine

# Verify thread utilization
meshrepair large.obj fixed.obj -v 2 --threads 0
```

### Memory Constraints

For meshes exceeding available RAM:
- Use binary PLY format for I/O
- Process in sections if possible
- Allocate approximately 4× mesh file size in RAM

---

## Command Reference

### Synopsis

```
meshrepair <input> <output> [options]
meshrepair --engine [engine-options]
```

### General Options

| Option | Default | Description |
|--------|---------|-------------|
| `-h, --help` | - | Display help message |
| `-v, --verbose <0-4>` | 1 | Verbosity level |
| `--validate` | off | Validate mesh topology |
| `--temp-dir <path>` | - | Debug output directory |

### Hole Filling Options

| Option | Default | Description |
|--------|---------|-------------|
| `--continuity <0\|1\|2>` | 1 | Surface continuity level |
| `--max-boundary <n>` | 1000 | Maximum hole boundary vertices |
| `--max-diameter <r>` | 0.1 | Maximum hole diameter ratio |
| `--no-refine` | off | Disable patch refinement |
| `--no-2d-cdt` | off | Disable 2D triangulation |
| `--no-3d-delaunay` | off | Disable 3D triangulation fallback |
| `--skip-cubic` | off | Skip cubic search algorithm |
| `--holes_only` | off | Return only patched holes (partitioned mode only) |
| `--min-edges <n>` | 100 | Minimum boundary edges per partition (partitioned mode) |

### Preprocessing Options

| Option | Default | Description |
|--------|---------|-------------|
| `--no-preprocess` | off | Skip all preprocessing |
| `--no-remove-duplicates` | off | Keep duplicate vertices |
| `--no-remove-non-manifold` | off | Keep non-manifold geometry |
| `--no-remove-3facefan` | off | Keep 3-face fan configurations |
| `--no-remove-isolated` | off | Keep isolated vertices |
| `--no-remove-small` | off | Keep small components |

### Performance Options

| Option | Default | Description |
|--------|---------|-------------|
| `--threads <n>` | 0 (auto) | Worker thread count |
| `--queue-size <n>` | 10 | Pipeline queue size (legacy mode) |
| `--no-partition` | off | Disable partitioned processing (use legacy pipeline) |
| `--cgal-loader` | off | Force CGAL OBJ loader |

Partitioned mode caps partitions to the number of holes and a minimum edge budget (`--min-edges`) to avoid oversharding tiny holes; lowering `--min-edges` increases parallelism on very small holes at the cost of overhead.

### Output Options

| Option | Default | Description |
|--------|---------|-------------|
| `--ascii-ply` | off | Write PLY in ASCII format |


---

## Related Documentation

- [Blender Addon Guide](blender-addon-guide.md) - Interactive repair within Blender
- [Index](index.md) - Project overview

---

## References

Hole filling algorithm:
> Peter Liepa. "Filling Holes in Meshes." *Eurographics Symposium on Geometry Processing*, 2003.

Fairing algorithm:
> Mario Botsch et al. "On Linear Variational Surface Deformation Methods." *IEEE Transactions on Visualization and Computer Graphics*, 2008.
