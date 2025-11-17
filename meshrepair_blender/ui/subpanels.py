# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
# ##### END GPL LICENSE BLOCK #####

import bpy
from bpy.types import Panel
from ..preferences import get_prefs


class MESHREPAIR_PT_SubPanel(Panel):
    """Base class for subpanels"""
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Mesh Repair'
    bl_parent_id = 'MESHREPAIR_PT_Main'
    bl_options = {'DEFAULT_CLOSED'}

    # No poll() method - subpanels always visible


class MESHREPAIR_PT_Preprocessing(MESHREPAIR_PT_SubPanel):
    """Preprocessing options panel"""
    bl_idname = 'MESHREPAIR_PT_Preprocessing'
    bl_label = 'Preprocessing Options'

    def draw_header(self, context):
        props = context.scene.meshrepair_props
        self.layout.prop(props, "enable_preprocessing", text="")

    def draw(self, context):
        layout = self.layout
        props = context.scene.meshrepair_props

        layout.enabled = props.enable_preprocessing

        col = layout.column(align=True)

        # Quick presets
        row = col.row(align=True)
        row.label(text="Preset:")
        op = row.operator("meshrepair.preset_preprocess", text="Light")
        op.preset = 'LIGHT'
        op = row.operator("meshrepair.preset_preprocess", text="Full")
        op.preset = 'FULL'

        col.separator()

        # Individual options
        box = col.box()
        box_col = box.column(align=True)
        box_col.label(text="Cleanup Operations:")
        box_col.prop(props, "preprocess_remove_duplicates")
        box_col.prop(props, "preprocess_remove_non_manifold")
        box_col.prop(props, "preprocess_remove_3_face_fans")
        box_col.prop(props, "preprocess_remove_isolated")
        box_col.prop(props, "preprocess_keep_largest")

        # Advanced
        col.separator()
        box = col.box()
        box_col = box.column(align=True)
        box_col.label(text="Advanced:")
        box_col.prop(props, "preprocess_nm_passes")
        box_col.prop(props, "preprocess_duplicate_threshold")


class MESHREPAIR_PT_Filling(MESHREPAIR_PT_SubPanel):
    """Hole filling options panel"""
    bl_idname = 'MESHREPAIR_PT_Filling'
    bl_label = 'Hole Filling Options'

    def draw(self, context):
        layout = self.layout
        props = context.scene.meshrepair_props

        col = layout.column(align=True)

        # Hole size limits (moved to top for visibility)
        box = col.box()
        box_col = box.column(align=True)
        box_col.label(text="Hole Size Limits:")
        box_col.prop(props, "filling_max_boundary")
        box_col.prop(props, "filling_max_diameter_ratio")

        # Quality settings
        col.separator()
        box = col.box()
        box_col = box.column(align=True)
        box_col.label(text="Quality Settings:")

        row = box_col.row(align=True)
        row.label(text="Continuity:")
        row.prop(props, "filling_continuity", text="")
        row.operator("meshrepair.help", icon='QUESTION', text="")

        box_col.prop(props, "filling_refine")
        box_col.prop(props, "filling_use_2d_cdt")
        box_col.prop(props, "filling_use_3d_delaunay")

        # Performance settings
        col.separator()
        box = col.box()
        box_col = box.column(align=True)
        box_col.label(text="Performance:")
        box_col.prop(props, "filling_skip_cubic")
        box_col.prop(props, "filling_use_partitioned")


class MESHREPAIR_PT_Results(MESHREPAIR_PT_SubPanel):
    """Results and statistics panel"""
    bl_idname = 'MESHREPAIR_PT_Results'
    bl_label = 'Results & Statistics'

    @classmethod
    def poll(cls, context):
        props = context.scene.meshrepair_props
        return props.has_results

    def draw(self, context):
        layout = self.layout
        props = context.scene.meshrepair_props

        col = layout.column(align=True)

        # Summary
        box = col.box()
        box_col = box.column(align=True)
        box_col.label(text="Last Operation Summary:", icon='INFO')

        row = box_col.row(align=True)
        row.label(text=f"Operation: {props.last_operation_type}")
        row = box_col.row(align=True)
        row.label(text=f"Status: {props.last_operation_status}")
        row = box_col.row(align=True)
        row.label(text=f"Time: {props.last_operation_time_ms / 1000:.2f}s")

        # Preprocessing results
        if props.last_preprocess_stats:
            col.separator()
            box = col.box()
            box_col = box.column(align=True)
            box_col.label(text="Preprocessing:", icon='MODIFIER')
            box_col.label(text=f"  Duplicates removed: {props.last_duplicate_count}", icon='BLANK1')
            box_col.label(text=f"  Non-manifold removed: {props.last_non_manifold_count}", icon='BLANK1')
            box_col.label(text=f"  3-face fans collapsed: {props.last_3_face_fan_count}", icon='BLANK1')
            box_col.label(text=f"  Isolated removed: {props.last_isolated_count}", icon='BLANK1')

        # Hole filling results
        if props.last_hole_stats:
            col.separator()
            box = col.box()
            box_col = box.column(align=True)
            box_col.label(text="Hole Filling:", icon='MOD_TRIANGULATE')
            box_col.label(text=f"  Holes detected: {props.last_holes_detected}", icon='BLANK1')
            box_col.label(text=f"  Holes filled: {props.last_holes_filled}", icon='BLANK1')
            box_col.label(text=f"  Holes failed: {props.last_holes_failed}", icon='BLANK1')
            box_col.label(text=f"  Holes skipped: {props.last_holes_skipped}", icon='BLANK1')

            col.separator()
            box = col.box()
            box_col = box.column(align=True)
            box_col.label(text="Geometry Added:", icon='MESH_DATA')
            box_col.label(text=f"  Vertices: {props.last_vertices_added}", icon='BLANK1')
            box_col.label(text=f"  Faces: {props.last_faces_added}", icon='BLANK1')

        # Actions
        col.separator()
        row = col.row(align=True)
        row.operator("meshrepair.export_stats", icon='EXPORT')
        row.operator("meshrepair.clear_stats", icon='X')


classes = (
    MESHREPAIR_PT_Preprocessing,
    MESHREPAIR_PT_Filling,
    MESHREPAIR_PT_Results,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)


def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
