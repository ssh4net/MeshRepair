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
        description="Path to meshrepair_engine executable",
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

    memory_limit_gb: IntProperty(
        name="Memory Limit (GB)",
        description="Maximum memory usage for engine",
        default=16,
        min=1,
        max=256
    )

    use_ramdisk: BoolProperty(
        name="Use Ramdisk",
        description="Use RAM disk for temporary files (faster I/O)",
        default=False
    )

    # Advanced
    temp_dir: StringProperty(
        name="Temp Directory",
        description="Directory for temporary files",
        default="",
        subtype='DIR_PATH'
    )

    log_level: EnumProperty(
        name="Log Level",
        description="Logging verbosity",
        items=[
            ('ERROR', "Error", "Errors only"),
            ('WARNING', "Warning", "Warnings and errors"),
            ('INFO', "Info", "Informational messages"),
            ('DEBUG', "Debug", "Detailed debug information"),
        ],
        default='INFO'
    )

    keep_temp_files: BoolProperty(
        name="Keep Temp Files",
        description="Don't delete temporary files (for debugging)",
        default=False
    )

    show_debug_info: BoolProperty(
        name="Show Debug Info",
        description="Show debug information in console (--verbose flag)",
        default=False
    )

    show_stats: BoolProperty(
        name="Show Statistics",
        description="Show detailed timing statistics for performance analysis (--stats flag)",
        default=False
    )

    # Socket mode (more stable than pipes)
    use_socket_mode: BoolProperty(
        name="Use Socket Mode",
        description="Connect to engine via TCP socket (more stable, allows manual engine startup)",
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
        col.label(text="Performance:", icon='SETTINGS')

        box = col.box()
        box_col = box.column(align=True)
        box_col.prop(self, "thread_count")
        box_col.prop(self, "memory_limit_gb")
        box_col.prop(self, "use_ramdisk")

        # Advanced
        layout.separator()
        col = layout.column(align=True)
        col.label(text="Advanced:", icon='PREFERENCES')

        box = col.box()
        box_col = box.column(align=True)
        box_col.prop(self, "temp_dir")
        box_col.prop(self, "log_level")
        box_col.prop(self, "keep_temp_files")
        box_col.prop(self, "show_debug_info")
        box_col.prop(self, "show_stats")

        # Socket Mode
        layout.separator()
        col = layout.column(align=True)
        col.label(text="Socket Mode (Advanced):", icon='NETWORK_DRIVE')

        box = col.box()
        box_col = box.column(align=True)
        box_col.prop(self, "use_socket_mode")

        if self.use_socket_mode:
            sub = box_col.box()
            sub_col = sub.column(align=True)
            sub_col.label(text="Manual Engine Startup:", icon='INFO')
            sub_col.label(text=f"  meshrepair --socket {self.socket_port}")
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
