# Mesh Repair Addon - Installation Instructions

## Quick Install (Recommended)

### Step 1: Locate Addon Directory

The addon is located at:
```
/mnt/w/VisualStudio/MeshRepair/meshrepair_blender/
```

### Step 2: Install in Blender

**Option A: Install via Blender UI**

1. Open Blender
2. Edit → Preferences (or Ctrl+Alt+U)
3. Go to "Add-ons" tab
4. Click "Install..." button
5. Navigate to `/mnt/w/VisualStudio/MeshRepair/meshrepair_blender/`
6. Select `__init__.py` file
7. Click "Install Add-on"
8. Enable the checkbox next to "Mesh: Mesh Repair"

**Option B: Manual Copy**

1. Find your Blender addons folder:
   - **Windows**: `%APPDATA%\Blender Foundation\Blender\<version>\scripts\addons\`
   - **Linux**: `~/.config/blender/<version>/scripts/addons/`
   - **macOS**: `~/Library/Application Support/Blender/<version>/scripts/addons/`

2. Copy entire `meshrepair_blender` folder to addons directory

3. Restart Blender

4. Enable addon:
   - Edit → Preferences → Add-ons
   - Search for "Mesh Repair"
   - Enable checkbox

### Step 3: Configure Engine Path

After enabling the addon:

1. In Preferences → Add-ons → Mesh Repair, click to expand
2. Either:
   - Click "Detect Engine" button (auto-detect)
   - Or manually set "Engine Path" to your `meshrepair_engine` executable

3. Click "Test Engine" to verify connection

---

## Testing the Addon (Without Engine)

The addon GUI is fully functional even without the engine:

1. Open Blender
2. Press `N` to open sidebar
3. Go to "Mesh Repair" tab
4. Select a mesh object
5. Explore the interface:
   - Try different operation modes
   - Adjust preprocessing settings
   - Change hole filling options
   - View system options

**Note**: Operations will show stub results until the engine is connected.

---

## Development Setup

If you're developing the addon:

### 1. Enable Blender Console (Windows)

1. Edit Blender shortcut
2. Add `--debug-python` to target

Or start Blender from command line:
```cmd
"C:\Program Files\Blender Foundation\Blender 5.0\blender.exe" --debug-python
```

### 2. Enable Auto-Reload

In Blender preferences:
- System → Development → Python → Enable "Auto Run Python Scripts"

### 3. Reload Addon After Changes

In Blender's Python Console:
```python
import bpy
bpy.ops.script.reload()
```

Or use F3 → "Reload Scripts"

---

## Verifying Installation

After installation, you should see:

### In Preferences (Edit → Preferences → Add-ons):
```
Search: "mesh repair"

☑ Mesh: Mesh Repair
   Professional mesh hole filling using CGAL
   [▼] Expand to see settings
```

### In 3D Viewport Sidebar (Press N):
```
Tabs: [Item] [Tool] [View] [Mesh Repair] ← New tab!
```

### In Mesh Repair Tab:
```
- Engine Status (collapsible)
- Main Operations
- Preprocessing Options (collapsible)
- Hole Filling Options (collapsible)
- Results & Statistics (collapsible)
- System Options (collapsible)
```

---

## Troubleshooting

### "Add-on not found" Error

**Cause**: Selected wrong file or folder

**Solution**: Select the `__init__.py` file inside `meshrepair_blender/` folder

### "Import Error" When Enabling

**Cause**: Missing dependencies or syntax error

**Solution**:
1. Check Blender console for error details
2. Verify all files are present
3. Check Python version compatibility (Blender 3.3+ uses Python 3.10+)

### Addon Appears but Tab Not Visible

**Cause**: Object not selected or wrong context

**Solution**:
1. Select a mesh object
2. Press `N` in 3D Viewport
3. Look for "Mesh Repair" tab on the right

### "Engine Not Found" Warning

**Expected**: Engine executable not installed yet

**Solution**:
1. Build `meshrepair_engine` (see ENGINE_CHANGES_REQUIRED.md)
2. Set path in System Options panel
3. Or click "Detect Engine" in preferences

---

## File Structure Check

Verify all files are present:

```
meshrepair_blender/
├── __init__.py              ✓ Main registration
├── preferences.py           ✓ Addon preferences
├── properties.py            ✓ Scene properties
├── README.md                ✓ Documentation
├── operators/
│   ├── __init__.py          ✓
│   ├── repair_operators.py  ✓
│   ├── utility_operators.py ✓
│   └── engine_operators.py  ✓
├── ui/
│   ├── __init__.py          ✓
│   ├── main_panel.py        ✓
│   └── subpanels.py         ✓
├── engine/
│   ├── __init__.py          ✓
│   ├── connection.py        ✓
│   └── manager.py           ✓
└── utils/
    ├── __init__.py          ✓
    └── mesh_utils.py        ✓
```

---

## Next Steps

1. ✅ Install addon in Blender
2. ✅ Verify GUI appears and works
3. ⏳ Build `meshrepair_engine` wrapper
4. ⏳ Connect addon to engine
5. ⏳ Test with real meshes

---

## Support

**Issues**: Check Blender console for error messages
**Help**: See README.md for usage instructions
**Development**: See BLENDER_ADDON_IMPLEMENTATION_PLAN.md

---

**Good luck! The GUI is ready to use.**
