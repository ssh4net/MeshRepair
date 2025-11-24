# MeshRepair for Blender - User Guide

> Blender addon for mesh hole detection and filling

MeshRepair for Blender integrates the MeshRepair engine directly into Blender's interface, providing access to hole detection and filling operations from the sidebar panel. This guide covers installation, configuration, and usage.

![Blender Addon Banner](https://ssh4net.github.io/MeshRepair/images/blender-preview.png)

---

## Table of Contents

- [Feature Summary](#feature-summary)
- [Installation](#installation)
- [Interface Overview](#interface-overview)
- [Preset Operations](#preset-operations)
- [Manual Operations](#manual-operations)
- [Edit Mode Operations](#edit-mode-operations)
- [Preprocessing Options](#preprocessing-options)
- [Hole Filling Options](#hole-filling-options)
- [Results and Statistics](#results-and-statistics)
- [Configuration Guidelines](#configuration-guidelines)
- [Troubleshooting](#troubleshooting)

---

## Feature Summary

| Feature | Description |
|---------|-------------|
| **Preset Modes** | C⁰, C¹, C² continuity presets |
| **Edit Mode Support** | Process selected faces only |
| **Preprocessing** | Topology cleanup before hole filling |
| **Statistics Display** | Operation results and timing |
| **Undo Support** | Full integration with Blender's undo system |
| **Multi-threaded** | Parallel processing via external engine |

---

## Installation

### Step 1: Install the Engine

The addon requires the MeshRepair engine executable:

1. Download from [Patreon](https://www.patreon.com/c/MadProcessor/posts?filters%5Btag%5D=meshrepair) or build from source
2. Extract to a permanent location:
   - **Windows**: `C:\Program Files\MeshRepair\`
   - **Linux**: `/usr/local/bin/` or `~/meshrepair/`
   - **macOS**: `/Applications/MeshRepair/` or `/usr/local/bin/`

### Step 2: Install the Addon

1. Download `meshrepair_blender.zip` from releases
2. In Blender: **Edit → Preferences → Add-ons**
3. Click **Install...** and select the ZIP file
4. Enable "Mesh: MeshRepair"

### Step 3: Configure Engine Path

1. In addon preferences, click **Detect Engine** for automatic detection
2. If detection fails, manually browse to the `meshrepair` executable
3. Click **Test Engine** to verify connectivity

![Addon Installation](https://ssh4net.github.io/MeshRepair/images/blender_preferences.png)

### Verification

The Engine Status panel should display:
- Status: Ready (green indicator)
- Engine version number

---

## Interface Overview

### Panel Location

1. Select a mesh object
2. Press **N** to open the sidebar
3. Select the **Mesh Repair** tab

### Panel Structure

![Interface Overview](https://ssh4net.github.io/MeshRepair/images/blender_addon_panel.png)


| Section | Function |
|---------|----------|
| **Engine Status** | Connection status and version (collapsible) |
| **Context Info** | Current mode and mesh/selection data |
| **Operation Buttons** | Preset or manual operation controls |
| **Preprocessing Options** | Topology cleanup settings (collapsible) |
| **Hole Filling Options** | Algorithm parameters (collapsible) |
| **Results** | Statistics from last operation |

### Context Display

The panel displays context information based on current mode:

| Mode | Information Displayed |
|------|----------------------|
| **Object Mode** | Object name, total face count |
| **Edit Mode** | Selected faces / total faces, scope selector |
| **No Selection** | Warning message |

---

## Preset Operations

For standard use cases, preset buttons provide configured parameter combinations:

### C⁰ (Fast)

| Parameter | Value |
|-----------|-------|
| Continuity | C⁰ (positional) |
| Refinement | Disabled |
| Cubic Search | Skipped |

Suitable for: Preview, large meshes, non-critical repairs

### C¹ (Standard)

| Parameter | Value |
|-----------|-------|
| Continuity | C¹ (tangent) |
| Refinement | Enabled |
| Cubic Search | Normal |

Suitable for: General use, balanced quality and performance

### C² (High Quality)

| Parameter | Value |
|-----------|-------|
| Continuity | C² (curvature) |
| Refinement | Enabled |
| Cubic Search | Normal |

Suitable for: Final output, additive manufacturing, high-fidelity requirements

---

## Manual Operations

For fine control, switch to **Custom** mode:

### Operation Sequence

#### 1. Preprocess Mesh

Executes topology cleanup operations.

Operations performed:
- Duplicate vertex merging
- Non-manifold geometry removal
- 3-face fan collapse
- Isolated vertex removal

#### 2. Detect Holes

Analyzes mesh and reports hole count.

Output:
- Total holes detected
- Hole size distribution

#### 3. Fill Holes

Fills detected holes using configured parameters.

Output:
- Holes filled successfully
- Holes failed
- Holes skipped (exceeded size limits)
- Geometry added (vertices, faces)

---

## Edit Mode Operations

The addon supports processing selected regions in Edit Mode.

### Use Cases

| Scenario | Benefit |
|----------|---------|
| **Selective repair** | Process specific areas only |
| **Preserve intentional openings** | Skip holes that should remain open |
| **Large mesh processing** | Reduce computation by limiting scope |
| **Iterative workflow** | Address problems incrementally |

### Scope Selection

In Edit Mode, the **Scope** option controls processing extent:

| Option | Behavior |
|--------|----------|
| **Selection** | Process selected faces and detected holes within |
| **Whole Mesh** | Process entire mesh regardless of selection |

### Selection Workflow

1. Enter Edit Mode (Tab)
2. Switch to Face Select mode (3)
3. Select faces surrounding holes to repair
4. Include adequate surrounding geometry for blending
5. Execute repair operation

### Selection Expansion

The addon automatically expands selection to include neighboring geometry for smooth blending. Control this behavior with the expansion parameter:

| Value | Behavior |
|-------|----------|
| **-1 (Auto)** | Calculated based on continuity level |
| **0** | No expansion (exact selection) |
| **1-8** | Manual expansion iterations |

Higher values provide smoother blending at increased computation cost.

### Selection Boundary Handling

The addon distinguishes between:
- **Selection boundaries**: Edges of the selected region (not holes)
- **Actual holes**: Gaps in the mesh surface

Selection boundaries are automatically excluded from hole filling.

---

## Preprocessing Options

Expand the Preprocessing panel to access cleanup settings:

### Presets

| Preset | Description |
|--------|-------------|
| **Light** | Duplicates and isolated vertices only |
| **Full** | All cleanup operations enabled |

### Individual Operations

| Option | Description | Default |
|--------|-------------|---------|
| **Remove Duplicates** | Merge coincident vertices | Enabled |
| **Remove Non-Manifold** | Remove invalid topology | Enabled |
| **Remove 3-Face Fans** | Collapse degenerate configurations | Enabled |
| **Remove Isolated** | Delete unconnected vertices | Enabled |
| **Keep Largest Only** | Remove small disconnected components | Disabled |

### Advanced Parameters

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| **Non-Manifold Depth** | 10 | 1-20 | Search recursion limit |
| **Duplicate Threshold** | 0.0001 | 0.0-1.0 | Distance for coincidence detection |

---

## Hole Filling Options

Expand the Hole Filling panel to access algorithm parameters:

### Size Limits

| Parameter | Default | Description |
|-----------|---------|-------------|
| **Max Boundary** | 1000 | Maximum hole boundary vertices |
| **Max Diameter** | 0.1 | Maximum diameter as ratio of mesh bbox |

Holes exceeding these limits are skipped.

### Quality Parameters

| Parameter | Options | Description |
|-----------|---------|-------------|
| **Continuity** | C⁰, C¹, C² | Surface smoothness level |
| **Refine Mesh** | On/Off | Match local triangle density |

### Continuity Levels


| Level | Description | Computation |
|-------|-------------|-------------|
| **C⁰** | Positional continuity | Low |
| **C¹** | Tangent continuity | Medium |
| **C²** | Curvature continuity | High |

### Algorithm Parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| **Use 2D Triangulation** | Enabled | Primary triangulation method |
| **Use 3D Delaunay** | Enabled | Fallback triangulation |
| **Skip Cubic Search** | Disabled | Skip exhaustive algorithm |
| **Partitioned Parallel** | Enabled | Multi-threaded processing |

Default values are suitable for most cases.

---

## Results and Statistics

After operations, the **Results** panel displays statistics:

### Summary

| Field | Description |
|-------|-------------|
| **Operation** | Type of operation performed |
| **Status** | Success or error indication |
| **Time** | Execution duration |

### Preprocessing Statistics

| Statistic | Description |
|-----------|-------------|
| **Duplicates** | Vertices merged |
| **Non-manifold** | Invalid geometry removed |
| **3-Face Fans** | Configurations collapsed |
| **Isolated** | Unconnected vertices removed |

### Hole Filling Statistics

| Statistic | Description |
|-----------|-------------|
| **Detected** | Total holes found |
| **Filled** | Successfully repaired |
| **Failed** | Unable to repair |
| **Skipped** | Exceeded size limits |
| **Vertices Added** | New vertices created |
| **Faces Added** | New triangles created |

---

## Configuration Guidelines

### 3D Scans

Recommended settings:
1. Enable preprocessing with all options
2. Enable "Keep Largest Only" to remove debris
3. Use C² continuity for smooth fills
4. Increase Max Diameter for large occlusion gaps

### Game Assets

Recommended settings:
1. Use C¹ continuity (balance of quality and performance)
2. Monitor triangle count (refinement adds geometry)
3. Use Edit Mode for targeted repairs

### Additive Manufacturing

Recommended settings:
1. Use C² continuity for smooth surfaces
2. Enable mesh refinement
3. Run preprocessing to ensure manifold output
4. Verify result with Blender's Mesh Analysis

### General Guidelines

| Recommendation | Rationale |
|----------------|-----------|
| Save before repair | Undo support exists, but saves provide fallback |
| Start with presets | Adjust parameters only if results are unsatisfactory |
| Use Edit Mode for large meshes | Reduces computation by limiting scope |
| Check statistics | Non-zero failed count indicates potential issues |

---

## Troubleshooting

### Engine Not Found

Resolution:
1. Open addon preferences
2. Click **Detect Engine** or browse manually
3. On Linux/macOS: verify executable permissions (`chmod +x meshrepair`)

### No Holes Detected

Possible causes:
- Mesh is watertight
- All holes exceed size limits
- Preprocessing required

Resolution:
1. Run preprocessing first
2. Increase Max Boundary and Max Diameter values
3. Inspect mesh in Edit Mode (Select All, check for boundaries)

### Filling Failures

Possible causes:
- Degenerate geometry
- Self-intersecting hole boundaries
- Complex non-planar holes

Resolution:
1. Disable "Use 2D Triangulation"
2. Run full preprocessing
3. Repair problematic areas manually in Edit Mode

### Performance Issues

Resolution:
1. Use C⁰ preset for initial testing
2. Process large meshes in sections using Edit Mode
3. Reduce Max Boundary to skip large holes
4. Verify thread count in addon preferences

### Unexpected Results

Resolution:
1. Undo (Ctrl+Z) and adjust parameters
2. Use Edit Mode for section-by-section repair
3. Run preprocessing before filling
4. Check for overlapping or self-intersecting geometry

### Debug Information

For issue reporting, enable verbose output:

1. Open addon preferences
2. Set **Verbosity** to 3 (Debug) or 4 (Trace)
3. Open system console:
   - Windows: **Window → Toggle System Console**
   - Linux/macOS: Launch Blender from terminal
4. Execute operation and capture console output

---

## Addon Preferences Reference

Access via **Edit → Preferences → Add-ons → Mesh: MeshRepair**

### Engine Settings

| Setting | Description |
|---------|-------------|
| **Engine Path** | Path to meshrepair executable |
| **Detect Engine** | Automatic path detection |
| **Test Engine** | Verify engine connectivity |

### Performance

| Setting | Default | Description |
|---------|---------|-------------|
| **Thread Count** | 8 | Worker threads (0 = automatic) |

### Debugging

| Setting | Default | Description |
|---------|---------|-------------|
| **Verbosity** | 1 (Info) | Output detail level (0-4) |
| **Temp Directory** | Empty | Debug file output location |
| **Socket Mode** | Disabled | TCP connection (for debugging) |

---

## Related Documentation

- [CLI Guide](cli-guide.md) - Command-line interface usage
- [Index](index.md) - Project overview

---

## References

Hole filling algorithm:
> Peter Liepa. "Filling Holes in Meshes." *Eurographics Symposium on Geometry Processing*, 2003.

Fairing algorithm:
> Mario Botsch et al. "On Linear Variational Surface Deformation Methods." *IEEE Transactions on Visualization and Computer Graphics*, 2008.
