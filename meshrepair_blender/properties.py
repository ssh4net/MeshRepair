# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
# ##### END GPL LICENSE BLOCK #####

import bpy
from bpy.props import (
    EnumProperty,
    IntProperty,
    FloatProperty,
    BoolProperty,
    StringProperty
)
from bpy.types import PropertyGroup


class MeshRepairSceneProperties(PropertyGroup):
    """Scene properties for Mesh Repair addon"""

    # Operation mode
    operation_mode: EnumProperty(
        name="Operation Mode",
        description="Select operation workflow",
        items=[
            ('FULL', "Full Repair", "Complete preprocessing and hole filling"),
            ('CUSTOM', "Custom Steps", "Manual step-by-step control"),
            ('PREPROCESS_ONLY', "Preprocess Only", "Stop after preprocessing"),
            ('DETECT_ONLY', "Detect Only", "Stop after hole detection"),
            ('FILL_ONLY', "Fill Only", "Skip preprocessing and detection"),
        ],
        default='FULL'
    )

    # Scope (Edit mode)
    mesh_scope: EnumProperty(
        name="Mesh Scope",
        description="Which part of mesh to process",
        items=[
            ('REMESH', "Remesh", "Treat selected faces as a hole boundary and fill it"),
            ('SELECTION', "Selection", "Process selected elements only"),
            ('WHOLE', "Whole Mesh", "Process entire mesh ignoring selection"),
        ],
        default='REMESH'
    )

    selection_dilation: IntProperty(
        name="Selection Expansion (0 = Auto)",
        description="Number of times to expand edit-mode selection before export (0 = auto)",
        default=0,
        min=0,
        max=8
    )

    # Preprocessing options
    enable_preprocessing: BoolProperty(
        name="Enable Preprocessing",
        description="Clean up mesh before hole filling",
        default=True
    )

    preprocess_remove_duplicates: BoolProperty(
        name="Remove Duplicate Vertices",
        description="Merge vertices at same location",
        default=True
    )

    preprocess_remove_non_manifold: BoolProperty(
        name="Remove Non-Manifold Vertices",
        description="Remove vertices with non-manifold topology",
        default=True
    )

    preprocess_remove_3_face_fans: BoolProperty(
        name="Remove 3-Face Fans",
        description="Collapse 3-triangle fans around central vertex into single triangle",
        default=True
    )

    preprocess_remove_isolated: BoolProperty(
        name="Remove Isolated Vertices",
        description="Remove vertices not connected to any face",
        default=True
    )

    preprocess_keep_largest: BoolProperty(
        name="Keep Largest Component Only",
        description="Remove all but the largest connected component",
        default=False
    )

    preprocess_nm_passes: IntProperty(
        name="Non-Manifold Max Depth",
        description="Maximum recursion depth for local non-manifold removal (typically converges in 2-3 iterations)",
        default=10,
        min=1,
        max=20
    )

    preprocess_duplicate_threshold: FloatProperty(
        name="Duplicate Threshold",
        description="Distance threshold for duplicate detection",
        default=0.0001,
        min=0.0,
        max=1.0,
        precision=6
    )

    # Hole filling options
    filling_continuity: EnumProperty(
        name="Continuity",
        description="Surface continuity at hole boundary",
        items=[
            ('0', "C⁰", "Position continuity only (fastest)"),
            ('1', "C¹", "Tangent continuity (recommended)"),
            ('2', "C²", "Curvature continuity (highest quality)"),
        ],
        default='1'
    )

    filling_refine: BoolProperty(
        name="Refine Mesh",
        description="Add vertices to match local mesh density",
        default=True
    )

    filling_use_2d_cdt: BoolProperty(
        name="Use 2D Triangulation",
        description="Use 2D Constrained Delaunay Triangulation",
        default=True
    )

    filling_use_3d_delaunay: BoolProperty(
        name="Use 3D Delaunay Fallback",
        description="Use 3D Delaunay if 2D fails",
        default=True
    )

    filling_skip_cubic: BoolProperty(
        name="Skip Cubic Search",
        description="Skip cubic search optimization (faster)",
        default=False
    )

    filling_use_partitioned: BoolProperty(
        name="Use Partitioned Parallel",
        description="Use advanced parallelization (5-10x faster)",
        default=True
    )

    filling_max_boundary: IntProperty(
        name="Max Boundary Vertices",
        description="Maximum hole size to fill (vertices)",
        default=1000,
        min=3,
        max=1000000
    )

    filling_max_diameter_ratio: FloatProperty(
        name="Max Diameter Ratio",
        description="Maximum hole diameter relative to mesh bbox",
        default=0.25,
        min=0.0,
        max=1.0,
        precision=3
    )

    # Results (read-only, updated by operators)
    has_results: BoolProperty(default=False)
    last_operation_type: StringProperty(default="")
    last_operation_status: StringProperty(default="")
    last_operation_time_ms: FloatProperty(default=0.0)

    # Preprocessing results
    last_preprocess_stats: BoolProperty(default=False)
    last_duplicate_count: IntProperty(default=0)
    last_non_manifold_count: IntProperty(default=0)
    last_3_face_fan_count: IntProperty(default=0)
    last_isolated_count: IntProperty(default=0)

    # Hole filling results
    last_hole_stats: BoolProperty(default=False)
    last_hole_count: IntProperty(default=0)
    last_holes_detected: IntProperty(default=0)
    last_holes_filled: IntProperty(default=0)
    last_holes_failed: IntProperty(default=0)
    last_holes_skipped: IntProperty(default=0)
    last_vertices_added: IntProperty(default=0)
    last_faces_added: IntProperty(default=0)


classes = (
    MeshRepairSceneProperties,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)


def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
