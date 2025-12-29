# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
# ##### END GPL LICENSE BLOCK #####

import bpy
import os
import platform
from bpy.types import AddonPreferences
from bpy.props import StringProperty, IntProperty, BoolProperty, EnumProperty


class MeshRepairPreferences(AddonPreferences):
    bl_idname = __package__

    # Engine configuration
    engine_path: StringProperty(
        name="Engine Path",
        description="Path to meshrepair executable (used with --engine for IPC mode)",
        default="",
        subtype='FILE_PATH'
    )

    engine_initialized: BoolProperty(
        name="Engine Initialized",
        description="Engine is ready to use",
        default=False
    )

    engine_version: StringProperty(
        name="Engine Version",
        description="Detected engine version",
        default=""
    )

    # Performance
    thread_count: IntProperty(
        name="Thread Count",
        description="Number of threads for parallel processing (0 = auto)",
        default=8,
        min=0,
        max=64
    )

    # Advanced
    temp_dir: StringProperty(
        name="Debug Output Directory",
        description="Directory for engine debug outputs (PLY dumps)",
        default="",
        subtype='DIR_PATH'
    )

    verbosity_level: EnumProperty(
        name="Verbosity Level",
        description="Engine output verbosity (0=Quiet, 1=Info/Stats, 2=Verbose, 3=Debug, 4=Trace/PLY dumps)",
        items=[
            ('0', "Quiet", "Minimal output - errors only"),
            ('1', "Info", "Timing statistics and summaries (Default)"),
            ('2', "Verbose", "Detailed progress and operation logs"),
            ('3', "Debug", "Full debug output and logging"),
            ('4', "Trace", "Debug + PLY file dumps (writes to Blender app folder)"),
        ],
        default='1'
    )

    # Socket mode (for debugging only)
    use_socket_mode: BoolProperty(
        name="Use Socket Mode",
        description="Connect to engine via TCP socket (for debugging only)",
        default=False
    )

    socket_host: StringProperty(
        name="Socket Host",
        description="Hostname or IP address for socket connection",
        default="localhost"
    )

    socket_port: IntProperty(
        name="Socket Port",
        description="TCP port for socket connection",
        default=9876,
        min=1024,
        max=65535
    )

    def draw(self, context):
        layout = self.layout

        # Engine Configuration
        col = layout.column(align=True)
        col.label(text="Engine Configuration:", icon='TOOL_SETTINGS')

        box = col.box()
        box_col = box.column(align=True)

        # Status indicator
        row = box_col.row(align=True)
        if self.engine_initialized:
            row.label(text="Status: Ready", icon='CHECKMARK')
        else:
            row.label(text="Status: Not Found", icon='ERROR')
            row.alert = True

        box_col.prop(self, "engine_path")

        row = box_col.row(align=True)
        row.operator("meshrepair.detect_engine", icon='VIEWZOOM')
        row.operator("meshrepair.test_engine", icon='PLAY')

        if self.engine_version:
            box_col.label(text=f"Version: {self.engine_version}", icon='INFO')

        # Performance
        layout.separator()
        col = layout.column(align=True)
        col.label(text="Engine Options:", icon='SETTINGS')

        box = col.box()
        box_col = box.column(align=True)
        box_col.prop(self, "thread_count")
        box_col.prop(self, "verbosity_level")

        # Warning for trace mode (PLY dumps)
        if self.verbosity_level == '4':
            warning_box = box_col.box()
            warning_col = warning_box.column(align=True)
            warning_col.alert = True
            warning_col.label(text="WARNING: Trace mode enabled", icon='ERROR')
            warning_col.label(text="PLY files will be written to temp folder")
            warning_col.label(text="This can consume significant disk space!")

        box_col.prop(self, "temp_dir")

        # Socket Mode (debugging only)
        layout.separator()
        col = layout.column(align=True)
        col.label(text="Socket Mode (Debugging Only):", icon='NETWORK_DRIVE')

        box = col.box()
        box_col = box.column(align=True)
        box_col.prop(self, "use_socket_mode")

        if self.use_socket_mode:
            sub = box_col.box()
            sub_col = sub.column(align=True)
            sub_col.label(text="Manual Engine Startup:", icon='INFO')
            sub_col.label(text=f"  meshrepair --engine --socket {self.socket_port}")
            sub_col.separator()
            sub_col.prop(self, "socket_host")
            sub_col.prop(self, "socket_port")


def get_prefs():
    """Get addon preferences"""
    return bpy.context.preferences.addons[__package__].preferences


classes = (
    MeshRepairPreferences,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)


def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
