# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
# ##### END GPL LICENSE BLOCK #####

import bpy
import bmesh
from bpy.types import Panel
from ..preferences import get_prefs


class MESHREPAIR_PT_Base(Panel):
    """Base class for all Mesh Repair panels"""
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = 'Mesh Repair'

    @classmethod
    def poll(cls, context):
        return True


class MESHREPAIR_PT_EngineStatus(MESHREPAIR_PT_Base):
    """Engine status panel"""
    bl_idname = 'MESHREPAIR_PT_EngineStatus'
    bl_label = 'Engine Status'
    bl_order = 0
    bl_options = {'DEFAULT_CLOSED'}

    def draw_header(self, context):
        prefs = get_prefs()
        layout = self.layout
        row = layout.row()

        # Status icon
        if prefs.engine_initialized:
            row.label(text="", icon='CHECKMARK')
        else:
            row.label(text="", icon='ERROR')
            row.alert = True

    def draw(self, context):
        layout = self.layout
        prefs = get_prefs()

        col = layout.column(align=True)

        if prefs.engine_initialized:
            box = col.box()
            box.label(text="Engine: Ready", icon='INFO')
            if prefs.engine_version:
                box.label(text=f"Version: {prefs.engine_version}")

        else:
            box = col.box()
            box.alert = True
            box.label(text="Engine not found!", icon='ERROR')
            box.label(text="Configure in addon preferences")

            col.separator()
            col.operator("meshrepair.detect_engine", icon='VIEWZOOM')


class MESHREPAIR_PT_Main(MESHREPAIR_PT_Base):
    """Main operations panel"""
    bl_idname = 'MESHREPAIR_PT_Main'
    bl_label = 'Main Operations'
    bl_order = 1

    # REMOVED poll() method - panel now always visible!

    def draw(self, context):
        layout = self.layout
        props = context.scene.meshrepair_props
        obj = context.active_object

        # Check if we have a valid mesh object
        has_mesh = obj is not None and obj.type == 'MESH'

        col = layout.column(align=True)

        # Mode selection - always enabled
        row = col.row(align=True)
        row.prop(props, "operation_mode", text="")
        row.operator("meshrepair.help", icon='QUESTION', text="")

        col.separator()

        # Context info
        box = col.box()
        box_col = box.column(align=True)

        if not has_mesh:
            # No mesh selected - show message
            box_col.label(text="No mesh selected", icon='INFO')
            box_col.label(text="Select a mesh object to enable operations", icon='BLANK1')

        elif obj.mode == 'EDIT':
            # Edit mode - show selection info
            bm = bmesh.from_edit_mesh(obj.data)
            selected_faces = sum(1 for f in bm.faces if f.select)
            total_faces = len(bm.faces)

            box_col.label(text="Mode: Edit", icon='EDITMODE_HLT')
            box_col.label(text=f"Selected: {selected_faces}/{total_faces} faces")

            # Scope override
            row = box_col.row(align=True)
            row.prop(props, "mesh_scope", expand=True)
            if props.mesh_scope == 'SELECTION':
                row = box_col.row(align=True)
                row.prop(props, "selection_dilation", text="Expand")

        elif obj.mode == 'OBJECT':
            # Object mode - show mesh info
            box_col.label(text="Mode: Object", icon='OBJECT_DATA')
            box_col.label(text=f"Mesh: {obj.name}")
            box_col.label(text=f"Faces: {len(obj.data.polygons)}")

        col.separator()

        # Operation buttons based on mode (disabled if no mesh)
        if props.operation_mode == 'CUSTOM':
            self.draw_custom_mode(col, props, has_mesh)
        else:
            self.draw_quick_mode(col, props, has_mesh)

    def draw_custom_mode(self, layout, props, enabled):
        """Draw individual step buttons for custom mode"""
        col = layout.column(align=True)
        col.enabled = enabled  # Disable all if no mesh

        # Step 1: Preprocess
        box = col.box()
        box_col = box.column(align=True)
        op = box_col.operator("meshrepair.preprocess", icon='MODIFIER', text="Preprocess Mesh")
        op.return_result = True

        if props.last_preprocess_stats:
            box_col.label(text=f"✓ Removed {props.last_duplicate_count} duplicates", icon='BLANK1')

        # Step 2: Detect
        box = col.box()
        box_col = box.column(align=True)
        op = box_col.operator("meshrepair.detect_holes", icon='VIEWZOOM', text="Detect Holes")
        op.return_result = True

        if props.last_hole_count > 0:
            box_col.label(text=f"✓ Found {props.last_hole_count} holes", icon='BLANK1')

        # Step 3: Fill
        box = col.box()
        box_col = box.column(align=True)
        row = box_col.row(align=True)
        row.operator("meshrepair.fill_holes", icon='MOD_TRIANGULATE', text="Fill Holes")
        if not enabled or props.last_hole_count == 0:
            row.enabled = False

        if props.last_holes_filled > 0:
            box_col.label(text=f"✓ Filled {props.last_holes_filled} holes", icon='BLANK1')

    def draw_quick_mode(self, layout, props, enabled):
        """Draw one-click preset buttons for quick mode"""
        col = layout.column(align=True)
        col.enabled = enabled  # Disable all if no mesh

        # Fast preset
        box = col.box()
        box_col = box.column(align=True)
        box_col.label(text="Fast Repair", icon='SORTTIME')
        box_col.label(text="C⁰ continuity, no refinement", icon='BLANK1')
        op = box_col.operator("meshrepair.repair_all", text="Repair (Fast)")
        op.preset = 'FAST'

        # Quality preset
        box = col.box()
        box_col = box.column(align=True)
        box_col.label(text="Quality Repair", icon='SHADING_RENDERED')
        box_col.label(text="C¹ continuity, with refinement", icon='BLANK1')
        op = box_col.operator("meshrepair.repair_all", text="Repair (Quality)")
        op.preset = 'QUALITY'

        # High quality preset
        box = col.box()
        box_col = box.column(align=True)
        box_col.label(text="High Quality Repair", icon='MATERIAL')
        box_col.label(text="C² continuity, full refinement", icon='BLANK1')
        op = box_col.operator("meshrepair.repair_all", text="Repair (High Quality)")
        op.preset = 'HIGH_QUALITY'


classes = (
    MESHREPAIR_PT_EngineStatus,
    MESHREPAIR_PT_Main,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)


def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
