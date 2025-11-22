# MeshRepair for Blender - User Guide

> **Seamless mesh repair integrated directly into your Blender workflow**

MeshRepair for Blender brings professional-grade hole filling and mesh cleanup directly into Blender's interface. No need to export, repair externally, and re-import—just select your mesh and click repair.

![Blender Addon Banner](images/blender-banner-placeholder.png)
*<!-- PLACEHOLDER: Hero image showing the addon panel in Blender with a repaired mesh -->*

---

## Table of Contents

- [Features at a Glance](#features-at-a-glance)
- [Installation](#installation)
- [Getting Started](#getting-started)
- [The Interface](#the-interface)
- [Quick Repair (Recommended)](#quick-repair-recommended)
- [Step-by-Step Repair](#step-by-step-repair)
- [Working in Edit Mode](#working-in-edit-mode)
- [Understanding the Options](#understanding-the-options)
- [Preprocessing Settings](#preprocessing-settings)
- [Hole Filling Settings](#hole-filling-settings)
- [Reading the Results](#reading-the-results)
- [Tips for Best Results](#tips-for-best-results)
- [Troubleshooting](#troubleshooting)

---

## Features at a Glance

| Feature | Description |
|---------|-------------|
| **One-Click Repair** | Fast, Quality, or High Quality presets |
| **Edit Mode Support** | Repair only selected faces |
| **Smart Cleanup** | Automatic topology fixes |
| **Live Statistics** | See exactly what was fixed |
| **Non-Destructive** | Full undo support |
| **Fast Processing** | Multi-threaded engine |

![Features Overview](images/features-overview-placeholder.png)
*<!-- PLACEHOLDER: Annotated screenshot showing key features of the addon -->*

---

## Installation

### Step 1: Get the Engine

The addon requires the MeshRepair engine to be installed:

1. Download the latest release from [GitHub Releases](https://github.com/your-repo/meshrepair/releases)
2. Extract to a permanent location:
   - **Windows**: `C:\Program Files\MeshRepair\`
   - **Linux**: `/usr/local/bin/` or `~/meshrepair/`
   - **macOS**: `/Applications/MeshRepair/` or `/usr/local/bin/`

### Step 2: Install the Addon

1. Download `meshrepair_blender.zip` from the releases page
2. In Blender, go to **Edit → Preferences → Add-ons**
3. Click **Install...** and select the downloaded ZIP file
4. Enable the addon by checking the box next to "Mesh: MeshRepair"

![Addon Installation](images/addon-install-placeholder.png)
*<!-- PLACEHOLDER: Screenshot of Blender preferences with addon installation highlighted -->*

### Step 3: Configure the Engine

1. In the addon preferences, click **Detect Engine**
2. If auto-detection fails, manually browse to the `meshrepair` executable
3. Click **Test Engine** to verify the connection

![Engine Configuration](images/engine-config-placeholder.png)
*<!-- PLACEHOLDER: Screenshot of addon preferences showing engine path configuration -->*

### Verify Installation

You should see:
- ✓ Green checkmark next to "Engine Status: Ready"
- Engine version number displayed

---

## Getting Started

### Opening the Panel

1. Select a mesh object in Object Mode (or Edit Mode)
2. Open the sidebar with **N** key
3. Click the **Mesh Repair** tab

![Panel Location](images/panel-location-placeholder.png)
*<!-- PLACEHOLDER: Screenshot showing how to find the Mesh Repair panel in the sidebar -->*

### Your First Repair

1. Select a mesh with holes
2. Click **Quality Repair**
3. Done! Check the results panel for statistics

![First Repair](images/first-repair-placeholder.png)
*<!-- PLACEHOLDER: Before/after screenshots of a simple repair in Blender -->*

---

## The Interface

### Main Panel Overview

![Interface Overview](images/interface-overview-placeholder.png)
*<!-- PLACEHOLDER: Annotated screenshot of the full addon interface -->*

| Section | Purpose |
|---------|---------|
| **Engine Status** | Shows if engine is ready (expandable) |
| **Context Info** | Current mode and mesh/selection info |
| **Quick Presets** | One-click repair buttons |
| **Custom Mode** | Step-by-step controls |
| **Preprocessing Options** | Mesh cleanup settings |
| **Hole Filling Options** | Quality and size settings |
| **Results** | Statistics from last operation |

### Mode Indicator

The panel shows your current context:

| Mode | Display |
|------|---------|
| **Object Mode** | Object name + total faces |
| **Edit Mode** | Selected faces / Total faces |
| **No Mesh Selected** | Warning message |

![Mode Indicator](images/mode-indicator-placeholder.png)
*<!-- PLACEHOLDER: Three screenshots showing different mode states -->*

---

## Quick Repair (Recommended)

For most users, the quick preset buttons are all you need:

### Fast Repair

![Fast Repair Button](images/fast-repair-button-placeholder.png)
*<!-- PLACEHOLDER: Close-up of Fast Repair button -->*

```
Speed: ★★★★★
Quality: ★★☆☆☆
```

**Best for:**
- Quick previews
- Large meshes (100K+ triangles)
- Non-critical repairs
- Speed testing

**Settings used:**
- C⁰ continuity (positional only)
- No mesh refinement
- Skip expensive algorithms

### Quality Repair

![Quality Repair Button](images/quality-repair-button-placeholder.png)
*<!-- PLACEHOLDER: Close-up of Quality Repair button -->*

```
Speed: ★★★☆☆
Quality: ★★★★☆
```

**Best for:**
- Most everyday repairs
- Game assets
- General 3D work
- Balanced quality/speed

**Settings used:**
- C¹ continuity (smooth tangents)
- Mesh refinement enabled
- Standard algorithms

### High Quality Repair

![High Quality Repair Button](images/highquality-repair-button-placeholder.png)
*<!-- PLACEHOLDER: Close-up of High Quality Repair button -->*

```
Speed: ★★☆☆☆
Quality: ★★★★★
```

**Best for:**
- Hero assets
- 3D printing preparation
- Medical/scientific models
- Final production meshes

**Settings used:**
- C² continuity (smooth curvature)
- Full mesh refinement
- All quality algorithms

### Visual Quality Comparison

![Quality Comparison](images/blender-quality-comparison-placeholder.png)
*<!-- PLACEHOLDER: Side-by-side render of same hole filled with Fast/Quality/High Quality -->*

---

## Step-by-Step Repair

For more control, switch to **Custom** mode:

![Custom Mode](images/custom-mode-placeholder.png)
*<!-- PLACEHOLDER: Screenshot of Custom mode interface -->*

### Step 1: Preprocess Mesh

Click **Preprocess Mesh** to clean up topology issues:

![Preprocess Step](images/preprocess-step-placeholder.png)
*<!-- PLACEHOLDER: Screenshot showing preprocessing button and results -->*

This fixes:
- Duplicate vertices at same location
- Non-manifold edges and vertices
- 3-triangle fans around single vertices
- Isolated (floating) vertices

**When to use:**
- Imported meshes from other software
- 3D scans with artifacts
- Meshes with known topology issues

### Step 2: Detect Holes

Click **Detect Holes** to analyze the mesh:

![Detect Step](images/detect-step-placeholder.png)
*<!-- PLACEHOLDER: Screenshot showing detect button and hole count results -->*

The results show:
- Total number of holes found
- This helps you decide on filling strategy

### Step 3: Fill Holes

Click **Fill Holes** to repair all detected holes:

![Fill Step](images/fill-step-placeholder.png)
*<!-- PLACEHOLDER: Screenshot showing fill button and results -->*

The results show:
- Holes successfully filled
- Holes that failed (usually degenerate geometry)
- Holes skipped (exceeded size limits)
- Vertices and faces added

---

## Working in Edit Mode

One of MeshRepair's most powerful features is **Edit Mode support**—repair only the parts you select!

### Why Use Edit Mode?

| Use Case | Benefit |
|----------|---------|
| **Selective repair** | Only fix specific areas |
| **Preserve features** | Keep intentional openings |
| **Large meshes** | Process smaller regions faster |
| **Iterative workflow** | Fix problems one at a time |

### Scope Options

When in Edit Mode, you'll see the **Scope** option:

![Scope Options](images/scope-options-placeholder.png)
*<!-- PLACEHOLDER: Screenshot showing Selection vs Whole Mesh radio buttons -->*

| Option | Behavior |
|--------|----------|
| **Selection** | Only process selected faces |
| **Whole Mesh** | Process entire mesh (ignore selection) |

### Selection Workflow

1. **Enter Edit Mode** (Tab key)
2. **Select faces** around holes you want to fill (face select mode: 3)
3. **Include surrounding area** (the addon auto-expands, but more is better)
4. Click **Repair**

![Edit Mode Selection](images/editmode-selection-placeholder.png)
*<!-- PLACEHOLDER: Screenshot showing face selection around a hole -->*

### Selection Expansion

The addon automatically expands your selection to include enough surrounding geometry for smooth blending. You can control this:

![Selection Expansion](images/selection-expansion-placeholder.png)
*<!-- PLACEHOLDER: Screenshot showing the expansion slider -->*

| Setting | Effect |
|---------|--------|
| **Auto (-1)** | Automatically calculated based on quality |
| **0** | No expansion (exact selection only) |
| **1-8** | Manual expansion iterations |

**Tip:** Higher expansion = smoother blending but slower processing

### Important: Selection Boundaries

When working with selections, the addon is smart about boundaries:

![Selection Boundary](images/selection-boundary-placeholder.png)
*<!-- PLACEHOLDER: Diagram showing selection boundary vs actual holes -->*

- **Selection boundary** = Edge of your selection (NOT a hole)
- **Actual holes** = Gaps in the mesh surface

The addon automatically distinguishes between these and only fills actual holes.

---

## Understanding the Options

### Preprocessing Panel

Click to expand preprocessing options:

![Preprocessing Panel](images/preprocessing-panel-placeholder.png)
*<!-- PLACEHOLDER: Screenshot of expanded preprocessing options -->*

#### Quick Presets

| Preset | Description |
|--------|-------------|
| **Light** | Basic cleanup (duplicates + isolated only) |
| **Full** | Complete cleanup (all options enabled) |

#### Individual Options

| Option | What it does | When to enable |
|--------|--------------|----------------|
| **Remove Duplicates** | Merges vertices at same location | Always (default) |
| **Remove Non-Manifold** | Fixes impossible geometry | Imported/scanned meshes |
| **Remove 3-Face Fans** | Cleans up pinch points | Problematic topology |
| **Remove Isolated** | Deletes floating vertices | Always (default) |
| **Keep Largest Only** | Removes small fragments | Scans with debris |

#### Advanced Settings

| Setting | Default | Description |
|---------|---------|-------------|
| **Non-Manifold Depth** | 10 | How aggressively to search for issues |
| **Duplicate Threshold** | 0.0001 | Distance to consider "same location" |

---

## Hole Filling Settings

Click to expand hole filling options:

![Filling Panel](images/filling-panel-placeholder.png)
*<!-- PLACEHOLDER: Screenshot of expanded hole filling options -->*

### Hole Size Limits

Control which holes get filled:

| Setting | Default | Description |
|---------|---------|-------------|
| **Max Boundary** | 1000 | Maximum vertices around hole edge |
| **Max Diameter** | 0.1 | Maximum hole size (10% of mesh) |

![Size Limits Visual](images/size-limits-visual-placeholder.png)
*<!-- PLACEHOLDER: Visual showing small holes filled, large hole skipped -->*

**Use cases:**
- Set higher limits for 3D scans with large gaps
- Set lower limits to skip intentional openings

### Quality Settings

| Setting | Options | Description |
|---------|---------|-------------|
| **Continuity** | C⁰, C¹, C² | Surface smoothness level |
| **Refine Mesh** | On/Off | Add vertices to match density |

#### Continuity Explained

![Continuity Comparison](images/continuity-comparison-placeholder.png)
*<!-- PLACEHOLDER: Visual showing C⁰/C¹/C² differences on same hole -->*

| Level | Name | Visual Effect | Performance |
|-------|------|---------------|-------------|
| **C⁰** | Positional | Flat patch, may show edges | Fastest |
| **C¹** | Tangent | Smooth blend, good quality | Balanced |
| **C²** | Curvature | Seamless blend, best quality | Slowest |

### Algorithm Settings

| Setting | Default | Description |
|---------|---------|-------------|
| **Use 2D Triangulation** | On | Primary fill method |
| **Use 3D Delaunay** | On | Fallback for complex holes |
| **Skip Cubic Search** | Off | Skip expensive algorithm |
| **Partitioned Parallel** | On | Multi-threaded processing |

**Tip:** Leave defaults unless troubleshooting specific issues.

---

## Reading the Results

After any operation, check the **Results** panel:

![Results Panel](images/results-panel-placeholder.png)
*<!-- PLACEHOLDER: Screenshot of results panel with all statistics -->*

### Summary Section

| Field | Meaning |
|-------|---------|
| **Operation** | What was performed |
| **Status** | Success or error message |
| **Time** | How long it took |

### Preprocessing Results

| Statistic | Meaning |
|-----------|---------|
| **Duplicates** | Vertices merged together |
| **Non-manifold** | Problematic geometry removed |
| **3-Face Fans** | Pinch points cleaned up |
| **Isolated** | Floating vertices removed |

### Hole Filling Results

| Statistic | Meaning | Good Value |
|-----------|---------|------------|
| **Detected** | Total holes found | Any |
| **Filled** | Successfully repaired | = Detected |
| **Failed** | Couldn't repair | 0 |
| **Skipped** | Exceeded size limits | 0 (or intentional) |
| **Vertices Added** | New geometry | Depends on holes |
| **Faces Added** | New triangles | Depends on holes |

---

## Tips for Best Results

### For 3D Scans

![3D Scan Tips](images/scan-tips-placeholder.png)
*<!-- PLACEHOLDER: Annotated 3D scan showing problem areas -->*

1. **Start with preprocessing** - Scans often have debris and artifacts
2. **Enable "Keep Largest Only"** - Removes floating fragments
3. **Use High Quality mode** - Scans benefit from smooth fills
4. **Increase Max Diameter** - Scans often have large occlusion gaps

### For Game Assets

1. **Use Quality mode** - Good balance for real-time rendering
2. **Check triangle count** - Refinement adds geometry
3. **Work in Edit Mode** - Repair specific areas as needed

### For 3D Printing

1. **Use High Quality mode** - Smooth surfaces print better
2. **Enable mesh refinement** - Consistent triangle size
3. **Run preprocessing** - Ensure watertight result
4. **Verify in Blender** - Use Mesh Analysis to check

### General Best Practices

| Do | Don't |
|----|-------|
| ✓ Save before repairing | ✗ Work on original file |
| ✓ Start with Quality preset | ✗ Jump to manual settings |
| ✓ Use Edit Mode for large meshes | ✗ Process million-poly meshes whole |
| ✓ Check results statistics | ✗ Ignore failed holes |

---

## Troubleshooting

### Engine Not Found

![Engine Not Found](images/engine-notfound-placeholder.png)
*<!-- PLACEHOLDER: Screenshot of error state -->*

**Solution:**
1. Go to addon preferences
2. Click **Detect Engine** or browse manually
3. Ensure the executable has run permissions (Linux/macOS)

### No Holes Detected

**Possible causes:**
- Mesh is already watertight
- Holes exceed size limits
- Mesh needs preprocessing first

**Solution:**
```
1. Run Preprocess first
2. Increase Max Boundary and Max Diameter
3. Check mesh in Edit Mode (select all, look for boundaries)
```

### Some Holes Failed

**Possible causes:**
- Degenerate geometry (zero-area triangles)
- Self-intersecting boundaries
- Very complex hole shapes

**Solution:**
```
1. Try disabling "Use 2D Triangulation"
2. Run preprocessing with all options
3. Manually fix problem areas in Edit Mode
```

### Slow Performance

**Solution:**
```
1. Use Fast preset for initial tests
2. Work in Edit Mode on large meshes
3. Reduce Max Boundary to skip huge holes
4. Check thread count in preferences
```

### Unexpected Results

![Unexpected Results](images/unexpected-results-placeholder.png)
*<!-- PLACEHOLDER: Example of a problem result -->*

**Solution:**
```
1. Undo (Ctrl+Z) and try different settings
2. Use Edit Mode to repair section by section
3. Try preprocessing first
4. Check for overlapping geometry
```

### Getting Debug Info

For bug reports, enable verbose output:

1. Go to addon preferences
2. Set **Verbosity** to 3 (Debug) or 4 (Trace)
3. Open **Window → Toggle System Console** (Windows) or launch Blender from terminal
4. Run the operation and check console output

---

## Keyboard Shortcuts

The addon doesn't install default shortcuts, but you can add them:

1. Right-click any button
2. Select **Assign Shortcut**
3. Press your desired key combination

**Suggested shortcuts:**
| Action | Suggested Key |
|--------|---------------|
| Quality Repair | Ctrl+Shift+R |
| Preprocess | Ctrl+Shift+P |
| Detect Holes | Ctrl+Shift+D |

---

## Addon Preferences Reference

Access via **Edit → Preferences → Add-ons → Mesh: MeshRepair**

![Preferences Panel](images/preferences-panel-placeholder.png)
*<!-- PLACEHOLDER: Screenshot of full preferences panel -->*

### Engine Settings

| Setting | Description |
|---------|-------------|
| **Engine Path** | Location of meshrepair executable |
| **Detect Engine** | Auto-find the engine |
| **Test Engine** | Verify connection |

### Performance

| Setting | Default | Description |
|---------|---------|-------------|
| **Thread Count** | 8 | Worker threads (0 = auto) |

### Debugging

| Setting | Default | Description |
|---------|---------|-------------|
| **Verbosity** | 1 (Info) | Output detail level |
| **Temp Directory** | Empty | Location for debug files |
| **Socket Mode** | Off | Advanced: TCP connection |

---

## Version History

| Version | Changes |
|---------|---------|
| 1.0.0 | Initial release |
| 1.1.0 | Added Edit Mode support |
| 1.2.0 | Selection boundary detection |

---

## Need More Help?

- **Command Line Users**: See the [CLI Guide](cli-guide.md)
- **Developers**: Check the [API Reference](api-reference.md)
- **Bug Reports**: [GitHub Issues](https://github.com/your-repo/meshrepair/issues)
- **Community**: [Blender Artists Thread](https://blenderartists.org/)

---

*MeshRepair for Blender is licensed under GPL v2.0, compatible with Blender's license.*
