# Mesh Repair Addon - Installation Instructions

## Prerequisites

- Blender 3.3 or newer (tested through Blender 4.x).
- Supported platforms: Windows 10/11 x64, Linux x64, macOS 12+ (Intel or Apple Silicon).
- Latest MeshRepair release archive (`MeshRepair_<version>_<platform>.zip`). Download from Patreon: https://www.patreon.com/c/MadProcessor/posts?filters%5Btag%5D=meshrepair

## What's in the release archive?

| File | Purpose |
| ---- | ------- |
| `meshrepair.exe` (Windows) or `meshrepair` (Linux/macOS) | C++ engine executable. The addon runs it with the `--engine` flag and refers to it as the *meshrepair_engine*. |
| `meshrepair_blender_<platform>.zip` | Blender addon package that you install through Blender's Add-on UI. |

> Tip: Keep both files together when you download a new build so you know which addon goes with which engine.

## Quick Install (Recommended)

### Step 1: Download and unpack

1. Download the latest `MeshRepair_<version>_<platform>.zip`.
2. Extract it to a temporary folder. You should see the engine executable and the addon ZIP listed above.

### Step 2: Install the engine executable

1. Copy the engine file to a permanent location:
   - **Windows**: `C:\Program Files\MeshRepair\meshrepair_engine.exe` (rename from `meshrepair.exe` if you want auto-detect to find it).
   - **Linux**: `/usr/local/bin/meshrepair_engine` (run `chmod +x meshrepair_engine`).
   - **macOS**: `/Applications/MeshRepair/meshrepair_engine` or `/usr/local/bin/meshrepair_engine`.
2. Make sure antivirus or Gatekeeper allows the binary to run.
3. (Optional) Add the folder to your PATH so future updates only require overwriting the binary.

### Step 3: Install the Blender addon

**Option A (recommended) – Install ZIP via Blender UI**

1. Launch Blender.
2. Open `Edit -> Preferences` (Ctrl+Alt+U).
3. Go to the `Add-ons` tab and click `Install...`.
4. Select `meshrepair_blender_<platform>.zip` from the release archive.
5. Click `Install Add-on`.
6. Enable the checkbox next to `Mesh: Mesh Repair`.

**Option B – Manual install (advanced)**

1. Copy the `meshrepair_blender` folder into your local `scripts/addons` directory.
2. Restart Blender and enable `Mesh: Mesh Repair` in the Add-ons list.

### Step 4: Configure the addon

1. In `Preferences -> Add-ons -> Mesh: Mesh Repair`, expand the entry.
2. Set **Engine Path**:
   - Click **Browse** and point to your engine executable (any filename works).
   - Or click **Detect Engine** to scan common locations.
3. Click **Test Engine**. A success toast shows the engine version and build info.
4. Close Preferences. The `Engine Status` panel in the 3D Viewport should now show **Engine: Ready**.

**Auto-detect search paths**

- Windows: `C:\Program Files\MeshRepair\meshrepair_engine.exe`, `<addon>/../build/meshrepair_engine/Release/meshrepair_engine.exe`
- Linux: `/usr/local/bin/meshrepair_engine`, `/usr/bin/meshrepair_engine`, `<addon>/../build/meshrepair_engine/meshrepair_engine`
- macOS: `/usr/local/bin/meshrepair_engine`, `/Applications/MeshRepair/meshrepair_engine`, `<addon>/../build/meshrepair_engine/meshrepair_engine`

### Step 5: Optional engine preferences

After `Engine Path` is set you can tweak:

- **Thread Count** – `0` lets the engine auto-detect cores. Set a specific value (1-64) to cap CPU usage.
- **Verbosity Level** – `0=Quiet`, `1=Info (default)`, `2=Verbose`, `3=Debug`, `4=Trace`. Trace writes detailed logs and PLY dumps; set `Debug Output Directory` first.
- **Debug Output Directory** – Where PLY dumps/logs are written when Verbosity is `4`. Leave blank to use Blender's temp directory.
- **Use Socket Mode (debug only)** – Enables remote engines. When checked, specify `Socket Host` and `Socket Port`, then manually launch the engine in a terminal: `meshrepair --engine --socket <port>`. Leave this disabled for normal usage.

---

## Testing the Addon (Without Engine)

The UI remains fully interactable even without the engine process:

1. Open Blender.
2. Press `N` to open the right-hand sidebar.
3. Switch to the `Mesh Repair` tab.
4. Select any mesh object.
5. Explore the panels (operation mode selector, preprocessing, hole filling, results).

All operations will show stub statistics until a real engine is connected, but this is useful for UI exploration or demoing.

---

## Verifying Installation

After completing the steps above you should see:

### Blender Preferences
```
Edit -> Preferences -> Add-ons
Search: "mesh repair"

☑ Mesh: Mesh Repair
    Professional mesh hole filling using CGAL
    [▼] Engine Path, Detect Engine, Test Engine, Thread Count, Verbosity, Debug Output, Socket Mode
```

### 3D Viewport Sidebar (`N` key)
```
Tabs: [Item] [Tool] [View] [Mesh Repair]
```

### Mesh Repair Tab Layout
```
- Engine Status (collapsible)
- Main Operations (Quick presets + Custom steps)
- Preprocessing Options
- Hole Filling Options
- Results & Statistics (appears after first run)
```

If the `Engine Status` box shows a red "Engine not found" message, return to Preferences and confirm the path/test steps.

---

## Troubleshooting

- **Add-on installs but Detect Engine fails**: Place the executable in one of the listed search paths or browse to it manually. Renaming the file to `meshrepair_engine(.exe)` helps auto-detect locate it.
- **Test Engine reports "executable not found"**: Ensure the path points to a real file and that it is marked executable (`chmod +x` on Linux/macOS). On Windows disable any SmartScreen blocks.
- **Test Engine hangs or errors**: Temporarily set Verbosity to `2`, click Test again, and check the Blender console for messages. Confirm that the engine is allowed through your firewall/antivirus.
- **Socket Mode is enabled unintentionally**: Open Preferences and uncheck `Use Socket Mode`. The addon defaults to managed subprocess mode; socket mode expects that you manually launch `meshrepair --engine --socket <port>` beforehand.
- **UI shows placeholder results**: This is expected until a repair is executed with a running engine. Click `Test Engine` to confirm connectivity, then run any preset.