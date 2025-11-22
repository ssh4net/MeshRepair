# MeshRepair Documentation

> **Professional mesh hole filling for 3D artists, game developers, and scanning professionals**

![MeshRepair Hero](images/hero-placeholder.png)
*<!-- PLACEHOLDER: Hero banner showing before/after mesh repair -->*

---

## What is MeshRepair?

MeshRepair is a powerful, open-source tool for automatically detecting and filling holes in 3D meshes. Whether you're working with 3D scans, game assets, or models for 3D printing, MeshRepair helps you create clean, watertight meshes quickly and reliably.

---

## Why This Tool Was Created

![RealityScan Workflow Problem](images/realityscan-problem-placeholder.png)
*<!-- PLACEHOLDER: Diagram showing RealityScan mesh with huge boundary polygons, then after removal showing holes -->*

MeshRepair was born from a real-world photogrammetry workflow challenge:

### The Problem with Photogrammetry Meshes

Tools like **Epic's RealityScan** (formerly RealityCapture) and similar photogrammetry software generate "watertight" meshes—but with a catch. To close the mesh, these tools create **enormous polygons** in problematic areas:

| Problem Area | What Happens |
|--------------|--------------|
| **Mesh boundaries** | Giant triangles spanning the entire edge |
| **Low overlap regions** | Huge faces covering areas with few photos |
| **Weak depth areas** | Oversized polygons hiding poor reconstruction |

![Problematic Polygons Example](images/huge-polygons-placeholder.png)
*<!-- PLACEHOLDER: Close-up of a RealityScan mesh showing giant boundary triangles -->*

### Why Remove Them?

These giant polygons cause problems for downstream work:

- **Sculpting**: Uneven topology makes brushes behave unpredictably
- **Retopology/Wrapping**: Automatic tools struggle with extreme polygon sizes
- **Texturing**: UV unwrapping produces distorted results
- **Subdivision**: Creates wildly uneven mesh density

### The Solution

**Remove the problematic polygons** → This creates holes → **Fill them properly with MeshRepair**

Unlike the original giant polygons, MeshRepair creates:
- **Evenly-sized triangles** matching surrounding mesh density
- **Smooth surfaces** that blend naturally with existing geometry
- **Clean topology** ready for sculpting and retopology

![Workflow Comparison](images/workflow-comparison-placeholder.png)
*<!-- PLACEHOLDER: 3-step comparison: 1) Original with huge polys 2) After removal (holes) 3) After MeshRepair (clean) -->*

### Built for High-Poly Meshes

Photogrammetry outputs are often **millions of polygons**. MeshRepair was designed from the ground up to handle these demanding meshes:

- **Multi-threaded processing** for fast repair of high-poly meshes
- **Partitioned parallel filling** for efficient memory usage
- **Optimized file I/O** with fast OBJ/PLY loaders
- **Edit Mode support** in Blender for working on sections of huge meshes

| Mesh Size | Typical Processing Time |
|-----------|------------------------|
| 100K triangles | < 1 second |
| 1M triangles | 2-5 seconds |
| 10M triangles | 15-30 seconds |
| 50M+ triangles | Minutes (use Edit Mode for sections) |

### Ideal Workflow

```
RealityScan/RealityCapture Export
        ↓
Remove huge boundary polygons (in Blender/ZBrush/etc.)
        ↓
MeshRepair (fill holes with quality geometry)
        ↓
Sculpt, Retopologize, or Wrap with clean base mesh
```

This workflow gives you the **best of both worlds**: the detail from photogrammetry with clean, workable topology for artist refinement.

### Key Features

| Feature | Description |
|---------|-------------|
| **Automatic Hole Detection** | Finds all gaps and openings in your mesh |
| **Smart Hole Filling** | Creates smooth, natural-looking patches |
| **Mesh Cleanup** | Fixes topology issues before repair |
| **Multiple Quality Levels** | From fast previews to production quality |
| **Blender Integration** | Native addon for seamless workflow |
| **Command Line Tool** | Batch processing and automation |
| **Multi-threaded** | Fast processing on multi-core systems |

---

## Choose Your Tool

<div class="tool-cards">

### Command Line Interface

For power users, batch processing, and automation.

![CLI Preview](images/cli-preview-placeholder.png)
*<!-- PLACEHOLDER: Terminal screenshot -->*

```bash
meshrepair input.obj output.obj
```

**[Read the CLI Guide →](cli-guide.md)**

---

### Blender Addon

For 3D artists who want seamless integration.

![Blender Preview](images/blender-preview-placeholder.png)
*<!-- PLACEHOLDER: Blender UI screenshot -->*

One-click repair directly in Blender's sidebar.

**[Read the Blender Guide →](blender-addon-guide.md)**

</div>

---

## Quick Start

### 5-Minute Setup

#### For Blender Users

1. **Download** the latest release
2. **Install** the addon in Blender preferences
3. **Configure** the engine path
4. **Select** a mesh and click **Quality Repair**

#### For CLI Users

1. **Download** the executable for your platform
2. **Run**: `meshrepair model.obj fixed.obj`
3. Done!

---

## How It Works

MeshRepair uses a sophisticated algorithm to repair meshes:

![Process Diagram](images/process-diagram-placeholder.png)
*<!-- PLACEHOLDER: Flowchart showing the 4 stages -->*

### 1. Preprocessing
Clean up mesh topology: remove duplicates, fix non-manifold geometry, remove debris.

### 2. Hole Detection
Find all boundary loops (holes) in the mesh surface.

### 3. Hole Filling
Fill each hole using Constrained Delaunay Triangulation with Laplacian fairing.

### 4. Output
Save the repaired, watertight mesh.

---

## Use Cases

### 3D Scanning

![Scan Use Case](images/usecase-scan-placeholder.png)
*<!-- PLACEHOLDER: Before/after of 3D scan -->*

Fill gaps from scanner occlusion. Perfect for photogrammetry, structured light, and LiDAR data.

### Game Development

![Game Use Case](images/usecase-game-placeholder.png)
*<!-- PLACEHOLDER: Before/after of game asset -->*

Prepare assets for engines that require closed meshes. Fix imported models with topology issues.

### 3D Printing

![Print Use Case](images/usecase-print-placeholder.png)
*<!-- PLACEHOLDER: Before/after with slicer preview -->*

Create watertight meshes required by slicers. Ensure successful prints every time.

### Digital Preservation

![Heritage Use Case](images/usecase-heritage-placeholder.png)
*<!-- PLACEHOLDER: Before/after of heritage artifact scan -->*

Restore scanned artifacts and cultural heritage objects with museum-quality repairs.

---

## Quality Levels

Choose the right quality for your needs:

| Level | Continuity | Speed | Best For |
|-------|------------|-------|----------|
| **Fast** | C⁰ | ★★★★★ | Previews, large meshes |
| **Quality** | C¹ | ★★★☆☆ | Most work, balanced |
| **High Quality** | C² | ★★☆☆☆ | Production, printing |

![Quality Comparison](images/quality-levels-placeholder.png)
*<!-- PLACEHOLDER: Side-by-side comparison of the three quality levels -->*

---

## System Requirements

### Minimum

- **OS**: Windows 10, Linux (Ubuntu 20.04+), macOS 11+
- **RAM**: 4 GB
- **CPU**: Dual-core processor
- **Blender**: 3.3+ (for addon)

### Recommended

- **RAM**: 16 GB
- **CPU**: 8+ cores (for multi-threaded processing)
- **Storage**: SSD for large meshes

---

## Documentation

| Guide | Description |
|-------|-------------|
| **[CLI Guide](cli-guide.md)** | Command line usage and options |
| **[Blender Addon Guide](blender-addon-guide.md)** | Complete addon documentation |
| **[FAQ](faq.md)** | Frequently asked questions |
| **[Troubleshooting](troubleshooting.md)** | Common issues and solutions |

---

## Getting Help

- **GitHub Issues**: [Report bugs](https://github.com/your-repo/meshrepair/issues)
- **Discussions**: [Ask questions](https://github.com/your-repo/meshrepair/discussions)
- **Blender Artists**: [Community thread](https://blenderartists.org/)

---

## License

MeshRepair is open-source software licensed under **GPL v2.0**.

The project uses:
- **CGAL** - Computational geometry algorithms
- **nlohmann/json** - JSON parsing
- **RapidOBJ** - Fast OBJ loading

---

## Acknowledgments

Hole filling algorithm based on:
> Peter Liepa. "Filling Holes in Meshes." *Eurographics Symposium on Geometry Processing*, 2003.

Fairing algorithm based on:
> Mario Botsch et al. "On Linear Variational Surface Deformation Methods." *IEEE TVCG*, 2008.

---

*Built with care for the 3D community.*
