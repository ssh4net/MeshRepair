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
from bpy.types import Operator
from ..preferences import get_prefs


class MESHREPAIR_OT_detect_engine(Operator):
    """Detect meshrepair_engine executable"""
    bl_idname = "meshrepair.detect_engine"
    bl_label = "Detect Engine"
    bl_description = "Automatically detect meshrepair_engine executable"

    def execute(self, context):
        prefs = get_prefs()

        # TODO: Implement actual engine detection
        # Search paths based on platform
        search_paths = []

        if platform.system() == "Windows":
            search_paths = [
                "C:\\Program Files\\MeshRepair\\meshrepair_engine.exe",
                os.path.join(os.path.dirname(__file__), "..", "..", "build", "meshrepair_engine", "Release", "meshrepair_engine.exe"),
            ]
        elif platform.system() == "Linux":
            search_paths = [
                "/usr/local/bin/meshrepair_engine",
                "/usr/bin/meshrepair_engine",
                os.path.join(os.path.dirname(__file__), "..", "..", "build", "meshrepair_engine", "meshrepair_engine"),
            ]
        elif platform.system() == "Darwin":  # macOS
            search_paths = [
                "/usr/local/bin/meshrepair_engine",
                "/Applications/MeshRepair/meshrepair_engine",
                os.path.join(os.path.dirname(__file__), "..", "..", "build", "meshrepair_engine", "meshrepair_engine"),
            ]

        # Search for engine
        found = False
        for path in search_paths:
            if os.path.exists(path):
                prefs.engine_path = path
                prefs.engine_initialized = True
                prefs.engine_version = "1.0.0"  # TODO: Get actual version
                found = True
                self.report({'INFO'}, f"Engine found: {path}")
                break

        if not found:
            prefs.engine_initialized = False
            self.report({'WARNING'}, "Engine not found in standard locations")
            return {'CANCELLED'}

        return {'FINISHED'}


class MESHREPAIR_OT_test_engine(Operator):
    """Test engine connection"""
    bl_idname = "meshrepair.test_engine"
    bl_label = "Test Engine"
    bl_description = "Test connection to meshrepair_engine"

    def execute(self, context):
        from ..engine.engine_session import EngineSession

        prefs = get_prefs()

        if not prefs.engine_path or not os.path.exists(prefs.engine_path):
            self.report({'ERROR'}, "Engine executable not found")
            return {'CANCELLED'}

        # Test engine by starting it and getting info
        try:
            session = EngineSession(
                prefs.engine_path,
                verbosity=int(prefs.verbosity_level),
                socket_mode=prefs.use_socket_mode,
                socket_host=prefs.socket_host,
                socket_port=prefs.socket_port,
                temp_dir=prefs.temp_dir
            )
            info = session.test()
            session.stop()

            # Extract info from response
            mesh_info = info.get('mesh_info', {})
            preprocessing_stats = info.get('preprocessing_stats', {})
            version = info.get('version', 'unknown')
            build_date = info.get('build_date', 'unknown')
            build_time = info.get('build_time', 'unknown')

            # Build info message
            msg = f"Engine test successful | "
            msg += f"Version: {version}, Built: {build_date} {build_time} | "
            msg += f"Mesh: {mesh_info.get('vertices', 0)} verts, {mesh_info.get('faces', 0)} faces"

            if preprocessing_stats:
                msg += f" | Preprocessing available"

            self.report({'INFO'}, msg)
            prefs.engine_initialized = True
            return {'FINISHED'}

        except Exception as ex:
            self.report({'ERROR'}, f"Engine test failed: {str(ex)}")
            prefs.engine_initialized = False
            return {'CANCELLED'}


class MESHREPAIR_OT_clear_cache(Operator):
    """Clear cache files"""
    bl_idname = "meshrepair.clear_cache"
    bl_label = "Clear Cache"
    bl_description = "Clear temporary cache files"

    def execute(self, context):
        # TODO: Implement cache clearing
        self.report({'INFO'}, "Cache cleared (stub)")
        return {'FINISHED'}


class MESHREPAIR_OT_reset_settings(Operator):
    """Reset all settings to defaults"""
    bl_idname = "meshrepair.reset_settings"
    bl_label = "Reset Settings"
    bl_description = "Reset all addon settings to default values"

    def execute(self, context):
        props = context.scene.meshrepair_props

        # Reset operation settings
        props.operation_mode = 'FULL'
        props.mesh_scope = 'SELECTION'
        props.selection_dilation = 0

        # Reset preprocessing
        props.enable_preprocessing = True
        props.preprocess_remove_duplicates = True
        props.preprocess_remove_non_manifold = True
        props.preprocess_remove_3_face_fans = True
        props.preprocess_remove_isolated = True
        props.preprocess_keep_largest = False
        props.preprocess_nm_passes = 10
        props.preprocess_duplicate_threshold = 0.0001

        # Reset filling
        props.filling_continuity = '1'
        props.filling_refine = True
        props.filling_use_2d_cdt = True
        props.filling_use_3d_delaunay = True
        props.filling_skip_cubic = False
        props.filling_use_partitioned = True
        props.filling_max_boundary = 1000
        props.filling_max_diameter_ratio = 0.1

        self.report({'INFO'}, "Settings reset to defaults")
        return {'FINISHED'}


classes = (
    MESHREPAIR_OT_detect_engine,
    MESHREPAIR_OT_test_engine,
    MESHREPAIR_OT_clear_cache,
    MESHREPAIR_OT_reset_settings,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)


def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
