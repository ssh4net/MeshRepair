# Mesh Repair - Blender Addon

Professional mesh hole filling and repair using CGAL algorithms.

## Features

- **Automatic Hole Filling**: Advanced algorithms for high-quality hole repair
- **Preprocessing**: Remove duplicates, non-manifold vertices, isolated vertices
- **Multiple Modes**: Full automatic repair or step-by-step control
- **Quality Presets**: Fast, Quality, and High Quality repair options
- **Edit Mode Support**: Repair selected faces or entire mesh
- **Parallel Processing**: Multi-threaded for fast processing of large meshes

## Installation

### Requirements

- **Blender**: 3.3 or later
- **meshrepair_engine**: C++ engine executable (must be installed separately)

### Steps

1. **Download or Clone** this repository
2. **Install Engine**: Build and install `meshrepair_engine` (see main project README)
3. **Install Addon**:
   - Open Blender
   - Edit → Preferences → Add-ons
   - Click "Install..."
   - Navigate to `meshrepair_blender` folder
   - Select any Python file (e.g., `__init__.py`)
   - Click "Install Add-on"
   - Enable the "Mesh Repair" addon

4. **Configure Engine Path**:
   - In Add-ons preferences, expand "Mesh Repair"
   - Set "Engine Path" to your `meshrepair_engine` executable
   - Or click "Detect Engine" to auto-detect

## Usage

### Quick Start

1. Select a mesh object
2. Open the 3D Viewport sidebar (press `N`)
3. Go to "Mesh Repair" tab
4. Click one of the quick repair buttons:
   - **Repair (Fast)**: C⁰ continuity, no refinement
   - **Repair (Quality)**: C¹ continuity, with refinement *(recommended)*
   - **Repair (High Quality)**: C² continuity, full refinement

### Advanced Usage

#### Custom Step-by-Step

1. Change "Operation Mode" to "Custom Steps"
2. Click each step individually:
   - **Preprocess Mesh**: Clean up topology
   - **Detect Holes**: Find all holes
   - **Fill Holes**: Fill detected holes
3. View results after each step

#### Edit Mode Selection

1. Enter Edit Mode (`Tab`)
2. Select faces around holes
3. In Mesh Repair panel:
   - Choose "Selection" to repair only selected area
   - Or choose "Whole Mesh" to ignore selection
4. Click repair button

### Settings

#### Preprocessing Options
- **Remove Duplicate Vertices**: Merge vertices at same location
- **Remove Non-Manifold Vertices**: Fix non-manifold topology
- **Remove Isolated Vertices**: Clean up disconnected vertices
- **Keep Largest Component**: Remove small disconnected pieces

#### Hole Filling Options
- **Continuity**: Surface smoothness at hole boundary
  - C⁰: Position continuity (fastest)
  - C¹: Tangent continuity (recommended)
  - C²: Curvature continuity (highest quality)
- **Refine Mesh**: Add vertices to match local density
- **Max Boundary Vertices**: Maximum hole size to fill
- **Max Diameter Ratio**: Skip very large holes

#### System Options
- **Thread Count**: Number of parallel threads (0 = auto)
- **Memory Limit**: Maximum memory usage
- **Use Ramdisk**: Use RAM disk for temporary files (Linux)

## Troubleshooting

### "Engine not found" Error

**Solution**: Set engine path in addon preferences:
1. Edit → Preferences → Add-ons → Mesh Repair
2. Set "Engine Path" to `meshrepair_engine` executable
3. Or click "Detect Engine"

### Operations Do Nothing

**Current Status**: This is a GUI preview version with stub implementations.

**To Enable Full Functionality**:
1. Build the `meshrepair_engine` wrapper (see ENGINE_CHANGES_REQUIRED.md)
2. Implement actual operations in `repair_operators.py`
3. Connect to engine via IPC

### No Holes Detected

**Possible Causes**:
- Mesh is already closed (no holes)
- Holes are larger than size limits
- Non-manifold geometry preventing detection

**Solution**:
- Check mesh in Edit mode
- Increase "Max Boundary Vertices" limit
- Enable preprocessing

## Development Status

**Current Version**: 1.0.0 (GUI Preview)

**What Works**:
- ✅ Full GUI with all panels
- ✅ All settings and properties
- ✅ Mode selection (Object/Edit)
- ✅ Selection support
- ✅ Results display (stub data)

**What Needs Implementation**:
- ⏳ Engine wrapper (C++)
- ⏳ Actual mesh export/import
- ⏳ IPC communication
- ⏳ Modal operator for progress
- ⏳ Real-time progress updates

## Project Structure

```
meshrepair_blender/
├── __init__.py              # Addon registration
├── preferences.py           # Addon preferences
├── properties.py            # Scene properties
├── operators/               # Operator implementations
│   ├── repair_operators.py  # Main repair operators
│   ├── utility_operators.py # Helper operators
│   └── engine_operators.py  # Engine management
├── ui/                      # User interface
│   ├── main_panel.py        # Main panel + engine status
│   └── subpanels.py         # Subpanels (settings, results)
├── engine/                  # Engine communication
│   ├── connection.py        # IPC protocol
│   └── manager.py           # Process management
└── utils/                   # Utilities
    └── mesh_utils.py        # Mesh import/export
```

## License

This program is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.

## Credits

- **CGAL**: Computational Geometry Algorithms Library
- **Liepa 2003**: "Filling Holes in Meshes" algorithm

## Support

- **Documentation**: See `MESH_REPAIR_GUI_SPECIFICATION.md`
- **Implementation Guide**: See `BLENDER_ADDON_IMPLEMENTATION_PLAN.md`
- **Issues**: Report bugs in the main project repository

---

**Version**: 1.0.0
**Blender**: 3.3+
**License**: GPL-2.0-or-later
