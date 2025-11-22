# MeshRepair CLI - User Guide

> **Professional mesh repair for 3D artists, game developers, and 3D scanning professionals**

MeshRepair is a powerful command-line tool for automatically detecting and filling holes in 3D meshes. Whether you're cleaning up 3D scans, preparing models for 3D printing, or fixing damaged game assets, MeshRepair provides fast, high-quality results.

![MeshRepair CLI Banner](images/cli-banner-placeholder.png)
*<!-- PLACEHOLDER: Hero image showing before/after of a repaired 3D scan -->*

---

## Table of Contents

- [Quick Start](#quick-start)
- [Understanding Mesh Holes](#understanding-mesh-holes)
- [The Repair Process](#the-repair-process)
- [Installation](#installation)
- [Basic Usage](#basic-usage)
- [Quality Presets](#quality-presets)
- [Advanced Options](#advanced-options)
- [Working with 3D Scans](#working-with-3d-scans)
- [Supported File Formats](#supported-file-formats)
- [Troubleshooting](#troubleshooting)
- [Command Reference](#command-reference)

---

## Quick Start

Repair a mesh with default settings (recommended for most cases):

```bash
meshrepair input.obj output.obj
```

That's it! MeshRepair will automatically:
1. Clean up mesh topology issues
2. Detect all holes
3. Fill holes with smooth, natural-looking patches
4. Save the repaired mesh

![Quick Start Example](images/quickstart-example-placeholder.png)
*<!-- PLACEHOLDER: Terminal screenshot showing basic command and output -->*

---

## Understanding Mesh Holes

### What are Mesh Holes?

A "hole" in a 3D mesh is a gap or opening where the surface is not continuous. Think of it like a hole in a piece of fabric - the edges around it form a boundary, but there's no surface filling the middle.

![Mesh Hole Diagram](images/hole-anatomy-placeholder.png)
*<!-- PLACEHOLDER: Diagram showing a mesh hole with labeled boundary edges and missing surface -->*

### Common Causes of Holes

| Source | Typical Hole Pattern | Example |
|--------|---------------------|---------|
| **3D Scanning** | Irregular gaps from occlusion | Scanner couldn't see behind ears on a head scan |
| **Boolean Operations** | Clean-cut openings | Subtraction that didn't fully close |
| **Incomplete Modeling** | Missing faces | Bottom of a vase left open |
| **File Conversion** | Random small holes | Format translation errors |
| **Corrupted Files** | Scattered missing triangles | Damaged mesh data |

![Hole Sources Examples](images/hole-sources-placeholder.png)
*<!-- PLACEHOLDER: Grid of 5 images showing each hole type -->*

### Why Repair Holes?

- **3D Printing**: Slicers require "watertight" meshes
- **Game Engines**: Holes cause rendering artifacts and physics issues
- **Simulation**: CFD and FEA need closed surfaces
- **Visualization**: Professional renders require clean geometry

---

## The Repair Process

MeshRepair uses a sophisticated multi-stage algorithm to repair your meshes:

### Stage 1: Preprocessing (Cleanup)

Before filling holes, MeshRepair cleans up common mesh problems:

![Preprocessing Stage](images/preprocessing-stage-placeholder.png)
*<!-- PLACEHOLDER: Diagram showing mesh before/after preprocessing -->*

| Problem | Fix Applied |
|---------|-------------|
| **Duplicate Vertices** | Merged to single vertex |
| **Non-Manifold Geometry** | Problematic vertices/edges removed |
| **3-Face Fans** | Collapsed to cleaner topology |
| **Isolated Vertices** | Floating points removed |
| **Small Fragments** | Tiny disconnected pieces removed |

### Stage 2: Hole Detection

MeshRepair analyzes the mesh to find all boundary loops (holes):

![Hole Detection](images/hole-detection-placeholder.png)
*<!-- PLACEHOLDER: Mesh with detected holes highlighted in different colors -->*

For each hole, the tool calculates:
- **Boundary size**: Number of vertices around the hole
- **Diameter**: Approximate size of the opening
- **Shape complexity**: How irregular the boundary is

### Stage 3: Hole Filling

Each hole is filled using the **Liepa Algorithm** with **Laplacian Fairing**:

![Hole Filling Process](images/filling-process-placeholder.png)
*<!-- PLACEHOLDER: 4-step diagram showing: 1) Hole boundary 2) Initial triangulation 3) Refinement 4) Fairing/smoothing -->*

1. **Triangulation**: Creates an initial patch using Constrained Delaunay Triangulation
2. **Refinement**: Adds vertices to match surrounding mesh density
3. **Fairing**: Smooths the patch to blend naturally with surrounding geometry

### Stage 4: Output

The repaired mesh is saved with:
- All holes filled
- Clean topology
- Preserved original detail
- Optimized file size

---

## Installation

### Windows

1. Download `meshrepair-windows.zip` from the [Releases page](https://github.com/your-repo/meshrepair/releases)
2. Extract to a folder (e.g., `C:\Tools\MeshRepair\`)
3. Add to PATH (optional but recommended):
   ```cmd
   setx PATH "%PATH%;C:\Tools\MeshRepair"
   ```

### Linux

```bash
# Download and extract
wget https://github.com/your-repo/meshrepair/releases/latest/meshrepair-linux.tar.gz
tar -xzf meshrepair-linux.tar.gz
sudo mv meshrepair /usr/local/bin/
```

### macOS

```bash
# Using Homebrew (recommended)
brew install meshrepair

# Or manual installation
wget https://github.com/your-repo/meshrepair/releases/latest/meshrepair-macos.tar.gz
tar -xzf meshrepair-macos.tar.gz
sudo mv meshrepair /usr/local/bin/
```

### Verify Installation

```bash
meshrepair --help
```

![Installation Verification](images/install-verify-placeholder.png)
*<!-- PLACEHOLDER: Terminal showing successful --help output -->*

---

## Basic Usage

### Simple Repair

```bash
meshrepair model.obj repaired.obj
```

### With Progress Information

```bash
meshrepair model.obj repaired.obj -v 2
```

Output example:
```
[INFO] Loading mesh: model.obj
[INFO] Vertices: 45,230  Faces: 89,456
[INFO] Preprocessing mesh...
  - Duplicates merged: 12
  - Non-manifold removed: 3
[INFO] Detecting holes...
  - Found 4 holes
[INFO] Filling holes...
  - Hole 1/4: 23 boundary vertices... filled (45 faces added)
  - Hole 2/4: 156 boundary vertices... filled (312 faces added)
  - Hole 3/4: 8 boundary vertices... filled (12 faces added)
  - Hole 4/4: 67 boundary vertices... filled (134 faces added)
[INFO] Saving: repaired.obj
[INFO] Complete! Time: 2.34s
```

### Converting Formats

```bash
# OBJ to PLY
meshrepair model.obj model.ply

# PLY to OBJ
meshrepair scan.ply scan.obj
```

---

## Quality Presets

Choose a preset based on your needs:

### Fast Mode (Speed Priority)

```bash
meshrepair model.obj fixed.obj --continuity 0 --no-refine --skip-cubic
```

| Setting | Value |
|---------|-------|
| Continuity | C⁰ (positional) |
| Refinement | Disabled |
| Quality | Basic |
| Speed | Fastest |

**Best for**: Quick previews, large meshes, non-critical repairs

![Fast Mode Result](images/fast-mode-placeholder.png)
*<!-- PLACEHOLDER: Before/after showing fast mode repair -->*

### Quality Mode (Recommended)

```bash
meshrepair model.obj fixed.obj --continuity 1
```

| Setting | Value |
|---------|-------|
| Continuity | C¹ (smooth tangents) |
| Refinement | Enabled |
| Quality | High |
| Speed | Balanced |

**Best for**: Most use cases, game assets, general 3D work

![Quality Mode Result](images/quality-mode-placeholder.png)
*<!-- PLACEHOLDER: Before/after showing quality mode repair -->*

### High Quality Mode (Maximum Quality)

```bash
meshrepair model.obj fixed.obj --continuity 2
```

| Setting | Value |
|---------|-------|
| Continuity | C² (smooth curvature) |
| Refinement | Enabled |
| Quality | Maximum |
| Speed | Slower |

**Best for**: Hero assets, 3D printing, medical/scientific visualization

![High Quality Mode Result](images/highquality-mode-placeholder.png)
*<!-- PLACEHOLDER: Before/after showing high quality mode repair with smooth curvature -->*

### Visual Comparison

![Quality Comparison](images/quality-comparison-placeholder.png)
*<!-- PLACEHOLDER: Side-by-side comparison of C⁰, C¹, C² on same hole -->*

---

## Advanced Options

### Hole Size Limits

Control which holes get filled based on size:

```bash
# Only fill holes with up to 500 boundary vertices
meshrepair model.obj fixed.obj --max-boundary 500

# Only fill holes smaller than 5% of mesh size
meshrepair model.obj fixed.obj --max-diameter 0.05
```

![Size Limits Diagram](images/size-limits-placeholder.png)
*<!-- PLACEHOLDER: Mesh showing which holes would be filled/skipped based on size -->*

### Preprocessing Control

Fine-tune the cleanup process:

```bash
# Skip all preprocessing (mesh is already clean)
meshrepair model.obj fixed.obj --no-preprocess

# Only remove duplicates
meshrepair model.obj fixed.obj --no-remove-non-manifold --no-remove-3facefan --no-remove-isolated
```

### Threading Performance

```bash
# Use 4 threads
meshrepair large_model.obj fixed.obj --threads 4

# Auto-detect optimal thread count (default)
meshrepair large_model.obj fixed.obj --threads 0
```

![Threading Performance](images/threading-chart-placeholder.png)
*<!-- PLACEHOLDER: Chart showing speedup with different thread counts -->*

### Debug Output

Generate intermediate files for troubleshooting:

```bash
meshrepair problem.obj fixed.obj --temp-dir ./debug -v 4
```

This creates:
- `debug_00_original_loaded.ply` - Mesh after loading
- `debug_06_partition_*.ply` - Partitioned submeshes
- `debug_07_partition_*_filled.ply` - After hole filling
- `debug_08_final_merged.ply` - Final result

---

## Working with 3D Scans

3D scans often have unique challenges. Here's how to handle them:

### Typical Scan Issues

![3D Scan Issues](images/scan-issues-placeholder.png)
*<!-- PLACEHOLDER: Annotated 3D scan showing common problem areas -->*

1. **Occlusion holes**: Areas the scanner couldn't see
2. **Edge artifacts**: Rough boundaries where scan ended
3. **Noise clusters**: Small disconnected fragments
4. **Thin features**: Hair, fingers, fine details with gaps

### Recommended Workflow for Scans

```bash
# Step 1: Heavy cleanup to remove scan artifacts
meshrepair raw_scan.ply cleaned.ply --preprocess-only

# Step 2: High-quality hole filling
meshrepair cleaned.ply final.ply --continuity 2 --max-boundary 2000
```

### Large Scan Files

For scans with millions of triangles:

```bash
# Use binary PLY for faster I/O
meshrepair scan.ply repaired.ply --threads 0

# If memory is limited, process in sections
# (use external decimation first, then repair, then subdivide)
```

### Preserving Scan Detail

```bash
# Maximum quality settings for important scans
meshrepair heritage_artifact.ply restored.ply \
    --continuity 2 \
    --max-diameter 0.15 \
    -v 2
```

![Scan Repair Example](images/scan-repair-placeholder.png)
*<!-- PLACEHOLDER: Before/after of a real 3D scan repair -->*

---

## Supported File Formats

| Format | Extension | Read | Write | Notes |
|--------|-----------|------|-------|-------|
| **Wavefront OBJ** | `.obj` | Yes | Yes | Most compatible, ASCII text |
| **Stanford PLY** | `.ply` | Yes | Yes | Binary (default) or ASCII |
| **Object File Format** | `.off` | Yes | Yes | CGAL native format |

### Format Tips

- **OBJ**: Best for compatibility with other software
- **PLY Binary**: Fastest for large files (default)
- **PLY ASCII**: Human-readable, use `--ascii-ply` flag

```bash
# Force ASCII PLY output
meshrepair model.obj output.ply --ascii-ply
```

---

## Troubleshooting

### Common Issues

#### "No holes detected"

Your mesh might already be watertight, or holes are too large:

```bash
# Check with verbose output
meshrepair model.obj fixed.obj -v 2

# Increase size limits
meshrepair model.obj fixed.obj --max-boundary 5000 --max-diameter 0.5
```

#### "Hole filling failed"

Some holes have degenerate geometry:

```bash
# Try different triangulation methods
meshrepair model.obj fixed.obj --no-2d-cdt

# Or skip problematic holes
meshrepair model.obj fixed.obj --max-boundary 100
```

#### Slow Performance

```bash
# Reduce quality for speed
meshrepair large.obj fixed.obj --continuity 0 --skip-cubic --no-refine

# Check thread usage
meshrepair large.obj fixed.obj -v 2 --threads 0
```

#### Memory Issues

For very large meshes (10M+ triangles):
- Use binary PLY format
- Process in sections if possible
- Ensure adequate system RAM (aim for 4x mesh file size)

### Getting Help

```bash
# Full option reference
meshrepair --help

# Version information
meshrepair --version
```

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
| `-h, --help` | - | Show help message |
| `-v, --verbose <0-4>` | 1 | Verbosity: 0=quiet, 1=info, 2=verbose, 3=debug, 4=trace |
| `--validate` | off | Validate mesh before/after processing |
| `--temp-dir <path>` | - | Directory for debug output files |

### Hole Filling Options

| Option | Default | Description |
|--------|---------|-------------|
| `--continuity <0\|1\|2>` | 1 | Surface continuity (C⁰/C¹/C²) |
| `--max-boundary <n>` | 1000 | Max hole boundary vertices |
| `--max-diameter <r>` | 0.1 | Max hole size (ratio of mesh bbox) |
| `--no-refine` | off | Disable patch refinement |
| `--no-2d-cdt` | off | Disable 2D triangulation |
| `--no-3d-delaunay` | off | Disable 3D fallback |
| `--skip-cubic` | off | Skip cubic search (faster) |

### Preprocessing Options

| Option | Default | Description |
|--------|---------|-------------|
| `--no-preprocess` | off | Skip all preprocessing |
| `--no-remove-duplicates` | off | Keep duplicate vertices |
| `--no-remove-non-manifold` | off | Keep non-manifold geometry |
| `--no-remove-3facefan` | off | Keep 3-face fans |
| `--no-remove-isolated` | off | Keep isolated vertices |
| `--no-remove-small` | off | Keep small components |

### Performance Options

| Option | Default | Description |
|--------|---------|-------------|
| `--threads <n>` | 0 (auto) | Number of worker threads |
| `--no-partition` | off | Use legacy pipeline |
| `--cgal-loader` | off | Force CGAL OBJ loader |

### Output Options

| Option | Default | Description |
|--------|---------|-------------|
| `--ascii-ply` | off | Save PLY as ASCII (not binary) |

---

## Examples Gallery

### Example 1: Simple Object Repair

![Example 1](images/example-simple-placeholder.png)
*<!-- PLACEHOLDER: Before/after of a simple object with small holes -->*

```bash
meshrepair vase.obj vase_fixed.obj
```

### Example 2: 3D Scan Restoration

![Example 2](images/example-scan-placeholder.png)
*<!-- PLACEHOLDER: Before/after of a human bust scan -->*

```bash
meshrepair bust_scan.ply bust_restored.ply --continuity 2 -v 2
```

### Example 3: Game Asset Cleanup

![Example 3](images/example-game-placeholder.png)
*<!-- PLACEHOLDER: Before/after of a game character model -->*

```bash
meshrepair character.obj character_clean.obj --max-diameter 0.05
```

### Example 4: 3D Print Preparation

![Example 4](images/example-print-placeholder.png)
*<!-- PLACEHOLDER: Before/after showing watertight mesh for printing -->*

```bash
meshrepair figurine.obj figurine_printready.obj --continuity 2 --validate
```

---

## Need More Help?

- **Blender Users**: Check out the [MeshRepair Blender Addon](blender-addon-guide.md) for an integrated experience
- **Developers**: See the [API Documentation](api-reference.md) for programmatic access
- **Issues**: Report bugs on [GitHub Issues](https://github.com/your-repo/meshrepair/issues)

---

*MeshRepair uses the CGAL library for computational geometry. Hole filling algorithm based on Liepa 2003 "Filling Holes in Meshes".*
