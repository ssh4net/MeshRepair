# Documentation Image Placeholders

This document lists all placeholder images needed for the MeshRepair documentation.
Create images with these filenames and replace the placeholders in the markdown files.

## Recommended Image Specifications

- **Format**: PNG (for screenshots), JPG (for renders)
- **Width**: 800-1200px for main content, 400-600px for thumbnails
- **Style**: Clean, professional, consistent lighting for 3D renders

---

## Index Page Images

### realityscan-problem-placeholder.png
**Description**: Diagram showing the RealityScan workflow problem
**Content**: 2-3 step diagram: 1) RealityScan mesh with visible huge boundary polygons 2) After polygon removal showing holes
**Size**: 900x400px
**Style**: Clean diagram with annotations, arrows showing workflow

### huge-polygons-placeholder.png
**Description**: Close-up of problematic photogrammetry polygons
**Content**: Wireframe view of a RealityScan/photogrammetry mesh showing giant triangles at boundaries vs normal-sized triangles in good areas
**Size**: 800x500px
**Style**: Wireframe overlay, highlight problem polygons in red

### workflow-comparison-placeholder.png
**Description**: 3-step workflow comparison
**Content**: Side-by-side: 1) Original mesh with huge polys (bad) 2) After removal with holes 3) After MeshRepair (clean fill)
**Size**: 1000x350px
**Style**: Clear labels "Before", "Holes", "After MeshRepair"

### hero-placeholder.png
**Description**: Hero banner for the main documentation page
**Content**: Split view showing a mesh with holes on the left, repaired mesh on the right
**Size**: 1200x400px
**Style**: Professional, maybe with subtle gradient background

### cli-preview-placeholder.png
**Description**: Preview of CLI tool
**Content**: Terminal window showing meshrepair command with colored output
**Size**: 600x300px

### blender-preview-placeholder.png
**Description**: Preview of Blender addon
**Content**: Blender interface with Mesh Repair panel visible in sidebar
**Size**: 600x300px

### process-diagram-placeholder.png
**Description**: Flowchart showing the repair process
**Content**: 4-step diagram: Preprocess → Detect → Fill → Output
**Size**: 800x200px
**Style**: Clean icons with arrows, minimal design

### usecase-scan-placeholder.png
**Description**: 3D scanning use case
**Content**: Before/after of a 3D scanned object (bust, artifact, etc.)
**Size**: 800x400px

### usecase-game-placeholder.png
**Description**: Game development use case
**Content**: Before/after of a game asset with holes
**Size**: 800x400px

### usecase-print-placeholder.png
**Description**: 3D printing use case
**Content**: Mesh with holes vs watertight mesh, maybe with slicer preview
**Size**: 800x400px

### usecase-heritage-placeholder.png
**Description**: Digital preservation use case
**Content**: Before/after of a cultural heritage artifact scan
**Size**: 800x400px

### quality-levels-placeholder.png
**Description**: Quality comparison
**Content**: Same hole filled with C⁰, C¹, C² side by side
**Size**: 900x300px

---

## CLI Guide Images

### cli-banner-placeholder.png
**Description**: Hero image for CLI documentation
**Content**: Terminal with meshrepair output, maybe split with 3D view
**Size**: 1200x300px

### quickstart-example-placeholder.png
**Description**: Quick start terminal example
**Content**: Simple terminal showing basic command and successful output
**Size**: 700x400px

### hole-anatomy-placeholder.png
**Description**: Diagram explaining what a mesh hole is
**Content**: Close-up of mesh with labeled boundary edges and missing surface
**Size**: 600x400px
**Style**: Technical diagram with labels

### hole-sources-placeholder.png
**Description**: Grid showing different sources of holes
**Content**: 5 examples: scan occlusion, boolean ops, incomplete modeling, conversion, corruption
**Size**: 1000x400px (grid layout)

### preprocessing-stage-placeholder.png
**Description**: Preprocessing visualization
**Content**: Before/after showing duplicate vertices merged, non-manifold removed
**Size**: 800x300px

### hole-detection-placeholder.png
**Description**: Detected holes visualization
**Content**: Mesh with multiple holes highlighted in different colors
**Size**: 700x400px

### filling-process-placeholder.png
**Description**: 4-step hole filling process
**Content**: Sequence: 1) Boundary 2) Triangulation 3) Refinement 4) Fairing
**Size**: 1000x250px

### install-verify-placeholder.png
**Description**: Installation verification
**Content**: Terminal showing successful --help output
**Size**: 600x300px

### fast-mode-placeholder.png
**Description**: Fast mode result
**Content**: Before/after with C⁰ repair
**Size**: 700x300px

### quality-mode-placeholder.png
**Description**: Quality mode result
**Content**: Before/after with C¹ repair
**Size**: 700x300px

### highquality-mode-placeholder.png
**Description**: High quality mode result
**Content**: Before/after with C² repair
**Size**: 700x300px

### quality-comparison-placeholder.png
**Description**: Side-by-side quality comparison
**Content**: Same hole with C⁰, C¹, C² repairs
**Size**: 900x300px

### size-limits-placeholder.png
**Description**: Size limits visualization
**Content**: Mesh showing which holes are filled vs skipped based on size
**Size**: 700x400px

### threading-chart-placeholder.png
**Description**: Threading performance chart
**Content**: Bar chart showing speedup with 1, 2, 4, 8, 16 threads
**Size**: 600x400px

### scan-issues-placeholder.png
**Description**: Common 3D scan issues
**Content**: Annotated scan showing occlusion, edge artifacts, noise
**Size**: 800x500px

### scan-repair-placeholder.png
**Description**: Real 3D scan repair example
**Content**: Dramatic before/after of actual scan repair
**Size**: 900x400px

### example-simple-placeholder.png
**Description**: Simple object repair example
**Content**: Before/after of simple object like a vase
**Size**: 700x300px

### example-scan-placeholder.png
**Description**: Scan repair example
**Content**: Before/after of human bust scan
**Size**: 700x300px

### example-game-placeholder.png
**Description**: Game asset example
**Content**: Before/after of game character or prop
**Size**: 700x300px

### example-print-placeholder.png
**Description**: 3D print prep example
**Content**: Before/after with print-ready verification
**Size**: 700x300px

---

## Blender Addon Guide Images

### blender-banner-placeholder.png
**Description**: Hero image for Blender documentation
**Content**: Blender window with Mesh Repair panel and repaired mesh
**Size**: 1200x400px

### features-overview-placeholder.png
**Description**: Annotated feature overview
**Content**: Blender interface with callouts pointing to key features
**Size**: 900x600px

### addon-install-placeholder.png
**Description**: Addon installation
**Content**: Blender preferences showing addon installation process
**Size**: 700x400px

### engine-config-placeholder.png
**Description**: Engine configuration
**Content**: Addon preferences with engine path highlighted
**Size**: 700x300px

### panel-location-placeholder.png
**Description**: How to find the panel
**Content**: Blender with sidebar open, Mesh Repair tab highlighted
**Size**: 600x400px

### first-repair-placeholder.png
**Description**: First repair walkthrough
**Content**: Before/after in Blender viewport
**Size**: 900x400px

### interface-overview-placeholder.png
**Description**: Full interface annotation
**Content**: Complete addon panel with all sections labeled
**Size**: 400x800px (tall)

### mode-indicator-placeholder.png
**Description**: Mode indicator states
**Content**: 3 screenshots showing Object/Edit/No Selection states
**Size**: 900x200px

### fast-repair-button-placeholder.png
**Description**: Fast repair button close-up
**Content**: Panel section showing Fast Repair button
**Size**: 300x100px

### quality-repair-button-placeholder.png
**Description**: Quality repair button close-up
**Content**: Panel section showing Quality Repair button
**Size**: 300x100px

### highquality-repair-button-placeholder.png
**Description**: High Quality button close-up
**Content**: Panel section showing High Quality Repair button
**Size**: 300x100px

### blender-quality-comparison-placeholder.png
**Description**: Quality comparison in Blender
**Content**: Same hole with three quality levels in viewport
**Size**: 900x300px

### custom-mode-placeholder.png
**Description**: Custom mode interface
**Content**: Panel showing step-by-step buttons
**Size**: 400x300px

### preprocess-step-placeholder.png
**Description**: Preprocess step
**Content**: Panel with preprocess button and results
**Size**: 400x250px

### detect-step-placeholder.png
**Description**: Detect step
**Content**: Panel with detect button and hole count
**Size**: 400x200px

### fill-step-placeholder.png
**Description**: Fill step
**Content**: Panel with fill button and results
**Size**: 400x300px

### scope-options-placeholder.png
**Description**: Scope radio buttons
**Content**: Close-up of Selection/Whole Mesh options
**Size**: 300x100px

### editmode-selection-placeholder.png
**Description**: Edit mode selection
**Content**: Blender in Edit mode with faces selected around hole
**Size**: 700x500px

### selection-expansion-placeholder.png
**Description**: Selection expansion slider
**Content**: Close-up of expansion control
**Size**: 300x80px

### selection-boundary-placeholder.png
**Description**: Selection boundary explanation
**Content**: Diagram showing selection edge vs actual hole
**Size**: 600x400px

### preprocessing-panel-placeholder.png
**Description**: Expanded preprocessing options
**Content**: Full preprocessing panel with all options visible
**Size**: 400x400px

### filling-panel-placeholder.png
**Description**: Expanded filling options
**Content**: Full hole filling panel with all options visible
**Size**: 400x500px

### size-limits-visual-placeholder.png
**Description**: Size limits visualization in Blender
**Content**: Mesh showing filled small holes, skipped large hole
**Size**: 700x400px

### continuity-comparison-placeholder.png
**Description**: Continuity visual comparison
**Content**: Close-up renders of C⁰, C¹, C² patches
**Size**: 900x300px

### results-panel-placeholder.png
**Description**: Results panel with statistics
**Content**: Full results panel showing all statistics
**Size**: 400x400px

### scan-tips-placeholder.png
**Description**: 3D scan tips
**Content**: Annotated scan showing problem areas and solutions
**Size**: 800x500px

### engine-notfound-placeholder.png
**Description**: Engine not found error
**Content**: Panel showing error state
**Size**: 400x200px

### unexpected-results-placeholder.png
**Description**: Problem result example
**Content**: Example of a repair that needs adjustment
**Size**: 600x300px

### preferences-panel-placeholder.png
**Description**: Full preferences panel
**Content**: Complete addon preferences view
**Size**: 500x400px

---

## Tips for Creating Images

### Screenshots
1. Use a clean Blender theme (default or similar)
2. Hide unnecessary panels for clarity
3. Use a simple mesh for examples (Stanford Bunny, simple shapes)
4. Ensure text is readable at documentation size

### 3D Renders
1. Use consistent lighting (3-point or HDRI)
2. Use neutral background (gradient gray or white)
3. Show wireframe overlay where helpful
4. Highlight problem areas with color

### Diagrams
1. Use consistent color palette
2. Keep text minimal and large
3. Use arrows to show flow/direction
4. Match documentation site styling

### Before/After Images
1. Use identical camera angle
2. Split view or side-by-side layout
3. Label "Before" and "After"
4. Consider animated GIF for web version

---

## Color Palette Suggestions

- **Problem areas**: Red (#E74C3C)
- **Fixed areas**: Green (#27AE60)
- **Highlights**: Blue (#3498DB)
- **Neutral**: Gray (#7F8C8D)
- **Background**: Light gray (#ECF0F1)
