# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
# ##### END GPL LICENSE BLOCK #####

import bpy
from bpy.types import Operator
from bpy.props import EnumProperty, StringProperty


class MESHREPAIR_OT_preset_preprocess(Operator):
    """Apply preprocessing preset"""
    bl_idname = "meshrepair.preset_preprocess"
    bl_label = "Apply Preset"
    bl_description = "Apply a preprocessing configuration preset"

    preset: EnumProperty(
        name="Preset",
        items=[
            ('LIGHT', "Light", "Basic cleanup - remove duplicates and isolated"),
            ('FULL', "Full", "Aggressive cleanup - all operations enabled"),
        ]
    )

    def execute(self, context):
        props = context.scene.meshrepair_props

        if self.preset == 'LIGHT':
            props.preprocess_remove_duplicates = True
            props.preprocess_remove_non_manifold = False
            props.preprocess_remove_3_face_fans = False
            props.preprocess_remove_isolated = True
            props.preprocess_keep_largest = False
            props.preprocess_nm_passes = 1  # Not used (non-manifold removal disabled)
        elif self.preset == 'FULL':
            props.preprocess_remove_duplicates = True
            props.preprocess_remove_non_manifold = True
            props.preprocess_remove_3_face_fans = True
            props.preprocess_remove_isolated = True
            props.preprocess_keep_largest = False
            props.preprocess_nm_passes = 10

        self.report({'INFO'}, f"Applied {self.preset} preprocessing preset")
        return {'FINISHED'}


class MESHREPAIR_OT_export_stats(Operator):
    """Export statistics to file"""
    bl_idname = "meshrepair.export_stats"
    bl_label = "Export Statistics"
    bl_description = "Export operation statistics to a file"

    def execute(self, context):
        props = context.scene.meshrepair_props

        if not props.has_results:
            self.report({'WARNING'}, "No results to export")
            return {'CANCELLED'}

        # TODO: Implement actual export
        self.report({'INFO'}, "Statistics export (stub)")
        return {'FINISHED'}


class MESHREPAIR_OT_clear_stats(Operator):
    """Clear statistics"""
    bl_idname = "meshrepair.clear_stats"
    bl_label = "Clear Statistics"
    bl_description = "Clear all operation statistics"

    def execute(self, context):
        props = context.scene.meshrepair_props

        # Clear all stats
        props.has_results = False
        props.last_operation_type = ""
        props.last_operation_status = ""
        props.last_operation_time_ms = 0.0

        props.last_preprocess_stats = False
        props.last_duplicate_count = 0
        props.last_non_manifold_count = 0
        props.last_3_face_fan_count = 0
        props.last_isolated_count = 0
        props.last_long_edge_count = 0

        props.last_hole_stats = False
        props.last_hole_count = 0
        props.last_holes_detected = 0
        props.last_holes_filled = 0
        props.last_holes_failed = 0
        props.last_holes_skipped = 0
        props.last_vertices_added = 0
        props.last_faces_added = 0

        self.report({'INFO'}, "Statistics cleared")
        return {'FINISHED'}


class MESHREPAIR_OT_help(Operator):
    """Open help documentation"""
    bl_idname = "meshrepair.help"
    bl_label = "Help"
    bl_description = "Open help documentation"

    topic: StringProperty(
        name="Topic",
        description="Help topic to display",
        default=""
    )

    def execute(self, context):
        # TODO: Open documentation URL
        if self.topic:
            self.report({'INFO'}, f"Help: {self.topic} (stub)")
        else:
            self.report({'INFO'}, "Help (stub)")
        return {'FINISHED'}


classes = (
    MESHREPAIR_OT_preset_preprocess,
    MESHREPAIR_OT_export_stats,
    MESHREPAIR_OT_clear_stats,
    MESHREPAIR_OT_help,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)


def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
