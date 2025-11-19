# Mesh Repair - Blender Addon

Professional mesh hole filling and repair using CGAL algorithms, integrated seamlessly into Blender.

## Features

- **Automatic Hole Filling**: Advanced algorithms for high-quality hole repair
- **Smart Preprocessing**: Automatically clean up duplicates, non-manifold vertices, and topology issues
- **Quality Presets**: Fast, Quality, and High Quality repair options
- **Edit Mode Support**: Repair selected faces or entire mesh
- **Parallel Processing**: Multi-threaded for fast processing of large meshes
- **Non-Destructive**: Original mesh remains unchanged until you apply results

## Installation

### Requirements

- **Blender**: 3.3 or later
- **meshrepair_engine**: C++ engine executable (prebuilt binaries available)

### Step 1: Download Engine Binary

**Prebuilt binaries** are available for download:

 **Download from Patreon**: https://www.patreon.com/c/MadProcessor/posts?filters%5Btag%5D=meshrepair

Available for:
- Windows (x64)
- Linux (x64)
- macOS (Intel & Apple Silicon)

### Step 2: Install Addon

1. **Download** the `meshrepair_blender` folder from this repository
2. Open **Blender**
3. Go to **Edit  Preferences  Add-ons**
4. Click **"Install..."** button
5. Navigate to the `meshrepair_blender` folder
6. Select the `__init__.py` file
7. Click **"Install Add-on"**
8. Enable the **"Mesh: Mesh Repair"** checkbox

### Step 3: Configure Engine

After enabling the addon:

1. In **Preferences  Add-ons**, expand **"Mesh Repair"**
2. Click **"Browse"** next to "Engine Path"
3. Select your downloaded `meshrepair_engine` executable
4. Click **"Test Engine"** to verify it works

**Auto-detect**: Click **"Detect Engine"** to automatically find the executable in common locations.

## Usage

### Quick Repair (Recommended)

1. **Select** a mesh object in Object Mode
2. Press **`N`** to open the sidebar
3. Go to **"Mesh Repair"** tab
4. Click one of the quick repair buttons:
   - **Repair (Fast)**: Quick repair, C continuity
   - **Repair (Quality)**: Balanced quality, C continuity *(recommended)*
   - **Repair (High Quality)**: Best quality, C continuity

The addon will automatically:
- Clean up the mesh (remove duplicates, fix topology)
- Detect all holes
- Fill holes with smooth patches
- Show results and statistics

### Advanced: Step-by-Step Mode

For more control over the repair process:

1. Change **"Operation Mode"** to **"Custom Steps"**
2. Run each step individually:
   - **Step 1: Preprocess Mesh** - Clean up topology
   - **Step 2: Detect Holes** - Find all holes in the mesh
   - **Step 3: Fill Holes** - Fill the detected holes
3. View results after each step in the **Results & Statistics** panel

### Repair Selected Area (Edit Mode)

To repair only part of a mesh:

1. Enter **Edit Mode** (`Tab`)
2. **Select faces** around the area you want to repair
3. In the **Mesh Repair** panel:
   - Set **"Mesh Scope"** to **"Selection"**
4. Click a repair button
5. Only the selected area will be processed

## Settings

### Preprocessing Options

Fine-tune mesh cleanup before hole filling:

- **Remove Duplicate Vertices**: Merge vertices at same location
- **Remove Non-Manifold Vertices**: Fix non-manifold topology (recommended)
- **Remove 3-Face Fans**: Simplify triangulated areas
- **Remove Isolated Vertices**: Clean up disconnected vertices
- **Keep Largest Component**: Remove small disconnected pieces

**Presets**:
- **Light**: Basic cleanup
- **Full**: Aggressive cleanup (recommended)

### Hole Filling Options

Control the quality and behavior of hole filling:

**Surface Quality**:
- **Continuity**: Smoothness at hole boundary
  - C: Position only (fastest)
  - C: Tangent continuity (recommended)
  - C: Curvature continuity (highest quality)
- **Refine Mesh**: Add vertices to match local density
- **Use Advanced Triangulation**: Better quality (slightly slower)

**Size Limits**:
- **Max Boundary Vertices**: Maximum hole size to fill (default: 1000)
- **Max Diameter Ratio**: Skip very large holes (default: 0.1)

**Performance**:
- **Use Partitioned Filling**: Parallel processing for multiple holes (recommended)

## Troubleshooting

### "Engine not found" Error

**Solution**:
1. Download the engine binary from Patreon (link above)
2. In addon preferences, set "Engine Path" to the downloaded executable
3. Click "Test Engine" to verify

### No Holes Detected

**Possible causes**:
- Mesh is already closed (no holes)
- Holes are larger than size limits
- Non-manifold geometry preventing detection

**Solutions**:
- Check mesh in Edit mode for actual holes
- Increase "Max Boundary Vertices" limit
- Enable all preprocessing options

### Repair Produces Weird Results

**Try**:
- Use "Full" preprocessing preset first
- Lower continuity level (C instead of C)
- Disable "Refine Mesh" for problem areas
- Use "Custom Steps" mode to see intermediate results

### Slow Performance

**Solutions**:
- Enable "Use Partitioned Filling" for parallel processing
- Use "Fast" preset for quick results
- Reduce "Max Boundary Vertices" to skip very large holes
- Close other applications to free up memory

## Results & Statistics

After running a repair operation, the **Results & Statistics** panel shows:

**Preprocessing Results**:
- Duplicates removed
- Non-manifold vertices fixed
- Isolated vertices cleaned

**Hole Filling Results**:
- Holes detected
- Holes filled successfully
- Holes failed (too large or complex)
- Holes skipped (size limits)
- Vertices/faces added

**Timing Breakdown**:
- File load time
- Preprocessing time (with sub-timings)
- Hole filling time
- File save time
- Total time

## Keyboard Shortcuts

While in the Mesh Repair tab:
- Press **`N`** - Toggle sidebar visibility
- **`Tab`** - Switch between Object/Edit mode
- **`Ctrl+Z`** - Undo last operation

## Tips & Best Practices

 **Always preprocess first** - Enable preprocessing for best results

 **Start with Quality preset** - Good balance of speed and quality

 **Use Selection mode** - For repairing specific problem areas

 **Check statistics** - Review results to understand what happened

 **Save before repairing** - Always save your .blend file first

 **Don't repair non-manifold meshes** - Preprocess first to fix topology

 **Don't expect miracles** - Very large or complex holes may fail

## System Requirements

**Minimum**:
- Blender 3.3+
- 4GB RAM
- Dual-core processor

**Recommended**:
- Blender 4.0+
- 16GB RAM
- Quad-core processor or better
- SSD for faster file I/O

## License

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

## Credits

- **CGAL**: Computational Geometry Algorithms Library
- **Liepa 2003**: "Filling Holes in Meshes" algorithm
- **Blender**: Open source 3D creation suite

## Support & Downloads

- **Download Engine**: https://www.patreon.com/c/MadProcessor/posts?filters%5Btag%5D=meshrepair
- **Report Issues**: Use GitHub Issues in the main repository
- **Documentation**: See INSTALL_INSTRUCTIONS.md for detailed setup

---

**Version**: 1.0.0
**Blender**: 3.3+
**License**: GPL-2.0-or-later
