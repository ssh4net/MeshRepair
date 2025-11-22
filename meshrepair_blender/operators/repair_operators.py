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
import os
import time
import sys
from bpy.types import Operator
from bpy.props import BoolProperty, EnumProperty
from ..preferences import get_prefs
from ..engine.engine_session import EngineSession
from ..utils.mesh_export import export_mesh_to_data, MeshExportResult
from ..utils.mesh_import import import_mesh_from_data


def console_log(level, message):
    """
    Log message to Blender console (System Console).

    Args:
        level: 'INFO', 'WARNING', 'ERROR'
        message: Message to log
    """
    prefix = f"[MeshRepair {level}]"
    full_message = f"{prefix} {message}"

    # Print to stdout (visible in System Console / terminal)
    print(full_message, file=sys.stdout)
    sys.stdout.flush()


class MESHREPAIR_OT_Base(Operator):
    """Base class for mesh repair operators"""

    @classmethod
    def poll(cls, context):
        return (context.active_object is not None and
                context.active_object.type == 'MESH')

    def check_engine(self):
        """Check if engine is available."""
        prefs = get_prefs()

        # In socket mode, engine path is not required (connecting to existing instance)
        if prefs.use_socket_mode:
            console_log('INFO', f"Socket mode: connecting to {prefs.socket_host}:{prefs.socket_port}")
            return True

        # In pipe mode, engine path is required
        if not prefs.engine_path or not os.path.exists(prefs.engine_path):
            msg = "Engine not found. Configure in addon preferences."
            self.report({'ERROR'}, msg)
            console_log('ERROR', msg)
            return False
        console_log('INFO', f"Using engine: {prefs.engine_path}")
        return True

    def console_report(self, level, message):
        """
        Report to both UI and console.

        Args:
            level: 'INFO', 'WARNING', 'ERROR'
            message: Message to report
        """
        # Report to UI status line
        self.report({level}, message)

        # Also log to console
        console_log(level, message)

    def _check_edit_mode_selection(self, context):
        """
        Check if Edit mode has a valid selection when selection scope is enabled.

        Returns:
            bool: True if OK to proceed, False if should cancel
        """
        obj = context.active_object
        if not obj or obj.mode != 'EDIT':
            return True  # Object mode - always OK

        props = context.scene.meshrepair_props
        if getattr(props, "mesh_scope", 'SELECTION') != 'SELECTION':
            return True  # Full mesh scope - always OK

        # Check if there's any selection in Edit mode
        bm = bmesh.from_edit_mesh(obj.data)
        has_selection = (
            any(f.select for f in bm.faces) or
            any(e.select for e in bm.edges) or
            any(v.select for v in bm.verts)
        )

        if not has_selection:
            msg = "No selection in Edit mode. Select faces/edges/vertices first, or switch to Object mode."
            self.report({'ERROR'}, msg)
            console_log('ERROR', msg)
            return False

        return True

    def _selection_settings(self, context):
        props = context.scene.meshrepair_props
        obj = context.active_object

        selection_only = (
            obj and obj.mode == 'EDIT' and
            getattr(props, "mesh_scope", 'SELECTION') == 'SELECTION'
        )

        dilation = 0
        if selection_only:
            dilation = getattr(props, "selection_dilation", -1)
            if dilation is None or dilation < 0:
                dilation = self._auto_selection_dilation(props)
        return selection_only, dilation

    def _auto_selection_dilation(self, props):
        try:
            continuity = int(getattr(props, "filling_continuity", '1'))
        except Exception:
            continuity = 1
        return max(1, min(3, continuity + 1))

    def _export_active_mesh(self, context):
        obj = context.active_object
        selection_only, dilation = self._selection_settings(context)
        export_result = export_mesh_to_data(
            obj,
            selection_only=selection_only,
            dilation_iters=dilation
        )

        if selection_only and not export_result.selection_only:
            self.console_report('WARNING', "Selection was empty; processed whole mesh instead.")
        elif export_result.selection_only and dilation > 0:
            self.console_report('INFO', f"Selection expanded by {dilation} iteration(s).")

        return export_result


class MESHREPAIR_OT_preprocess(MESHREPAIR_OT_Base):
    """Preprocess mesh (remove duplicates, non-manifold vertices, etc.)"""
    bl_idname = "meshrepair.preprocess"
    bl_label = "Preprocess Mesh"
    bl_description = "Clean up mesh topology before hole filling"
    bl_options = {'UNDO'}

    return_result: BoolProperty(
        name="Return Result",
        description="Import result back to Blender or keep for next step",
        default=False
    )

    def execute(self, context):
        if not self.check_engine():
            return {'CANCELLED'}

        if not self._check_edit_mode_selection(context):
            return {'CANCELLED'}

        prefs = get_prefs()
        props = context.scene.meshrepair_props
        obj = context.active_object

        start_time = time.time()
        session = None

        try:
            # Export mesh data (no temp files)
            self.console_report('INFO', "Exporting mesh data...")
            export_result = self._export_active_mesh(context)
            mesh_data = export_result.mesh_data
            console_log('INFO', f"Exported {len(mesh_data['vertices'])} vertices, {len(mesh_data['faces'])} faces")

            # Start engine session
            session = EngineSession(
                prefs.engine_path,
                verbosity=int(prefs.verbosity_level),
                socket_mode=prefs.use_socket_mode,
                socket_host=prefs.socket_host,
                socket_port=prefs.socket_port,
                temp_dir=prefs.temp_dir
            )

            # Load mesh via pipe
            self.console_report('INFO', "Loading mesh into engine...")
            session.load_mesh(mesh_data)

            # Build preprocessing options
            options = {
                "remove_duplicates": props.preprocess_remove_duplicates,
                "remove_non_manifold": props.preprocess_remove_non_manifold,
                "remove_3_face_fans": props.preprocess_remove_3_face_fans,
                "remove_isolated": props.preprocess_remove_isolated,
                "keep_largest_component": props.preprocess_keep_largest,
                "non_manifold_passes": props.preprocess_nm_passes,
                "duplicate_threshold": props.preprocess_duplicate_threshold
            }

            # Preprocess
            self.console_report('INFO', "Preprocessing mesh...")
            response = session.preprocess(options)

            # In batch mode without mesh retrieval, execute queued commands now
            if session.batch_mode and not self.return_result:
                session.flush_batch()
                resolved = session.get_last_response("preprocess")
                if resolved:
                    response = resolved

            # Extract stats (stats is at root level, not in 'data')
            stats = response.get('stats', {})
            props.last_preprocess_stats = True
            props.last_duplicate_count = stats.get('duplicates_merged', 0)
            props.last_non_manifold_count = stats.get('non_manifold_vertices_removed', 0)
            props.last_3_face_fan_count = stats.get('face_fans_collapsed', 0)
            props.last_isolated_count = stats.get('isolated_vertices_removed', 0)

            # Get result and import if requested
            if self.return_result:
                self.console_report('INFO', "Receiving repaired mesh...")
                result_mesh_data = session.save_mesh()

                # After executing batch, refresh response with actual stats
                if session.batch_mode:
                    resolved = session.get_last_response("preprocess")
                    if resolved:
                        response = resolved

                self.console_report('INFO', "Importing result...")
                import_mesh_from_data(
                    result_mesh_data,
                    obj,
                    replace=not export_result.selection_only,
                    selection_info=export_result if export_result.selection_only else None
                )

            # Update props
            elapsed_time = (time.time() - start_time) * 1000
            props.has_results = True
            props.last_operation_type = "Preprocess"
            props.last_operation_status = "Success"
            props.last_operation_time_ms = elapsed_time

            msg = f"Preprocessing complete: {props.last_duplicate_count} duplicates, " \
                  f"{props.last_non_manifold_count} non-manifold, " \
                  f"{props.last_isolated_count} isolated vertices removed ({elapsed_time:.1f}ms)"
            self.console_report('INFO', msg)
            return {'FINISHED'}

        except Exception as ex:
            msg = f"Preprocessing failed: {str(ex)}"
            self.console_report('ERROR', msg)
            props.last_operation_status = f"Error: {str(ex)}"
            return {'CANCELLED'}

        finally:
            if session:
                session.stop()


class MESHREPAIR_OT_detect_holes(MESHREPAIR_OT_Base):
    """Detect holes in mesh"""
    bl_idname = "meshrepair.detect_holes"
    bl_label = "Detect Holes"
    bl_description = "Find all holes in the mesh"
    bl_options = {'UNDO'}

    return_result: BoolProperty(
        name="Return Result",
        description="Stop after detection or continue to filling",
        default=False
    )

    def execute(self, context):
        if not self.check_engine():
            return {'CANCELLED'}

        if not self._check_edit_mode_selection(context):
            return {'CANCELLED'}

        prefs = get_prefs()
        props = context.scene.meshrepair_props
        obj = context.active_object

        start_time = time.time()
        session = None

        try:
            # Export mesh data (no temp files)
            self.report({'INFO'}, "Exporting mesh data...")
            export_result = self._export_active_mesh(context)
            mesh_data = export_result.mesh_data

            # Start engine session
            session = EngineSession(
                prefs.engine_path,
                verbosity=int(prefs.verbosity_level),
                socket_mode=prefs.use_socket_mode,
                socket_host=prefs.socket_host,
                socket_port=prefs.socket_port,
                temp_dir=prefs.temp_dir
            )

            # Load mesh via pipe (pass selection_info for boundary vertex handling)
            self.report({'INFO'}, "Loading mesh into engine...")
            session.load_mesh(mesh_data, selection_info=export_result if export_result.selection_only else None)

            # Build detection options (send both legacy and current keys)
            max_boundary = props.filling_max_boundary
            max_diameter = props.filling_max_diameter_ratio
            options = {
                "max_boundary": max_boundary,
                "max_hole_boundary_vertices": max_boundary,
                "max_diameter": max_diameter,
                "max_hole_diameter_ratio": max_diameter
            }

            # Detect holes
            self.console_report('INFO', "Detecting holes...")
            response = session.detect_holes(options)

            if session.batch_mode:
                session.flush_batch()
                resolved = session.get_last_response("detect_holes")
                if resolved:
                    response = resolved

            # Extract stats (stats is at root level)
            hole_stats = response.get('stats', {})

            props.last_hole_count = hole_stats.get('num_holes_detected', 0)
            props.last_holes_detected = hole_stats.get('num_holes_detected', 0)

            # Update props
            elapsed_time = (time.time() - start_time) * 1000
            props.has_results = True
            props.last_operation_type = "Detect Holes"
            props.last_operation_status = "Success"
            props.last_operation_time_ms = elapsed_time

            msg = f"Found {props.last_hole_count} holes in {elapsed_time:.1f}ms"
            self.console_report('INFO', msg)
            return {'FINISHED'}

        except Exception as ex:
            msg = f"Hole detection failed: {str(ex)}"
            self.console_report('ERROR', msg)
            props.last_operation_status = f"Error: {str(ex)}"
            return {'CANCELLED'}

        finally:
            if session:
                session.stop()


class MESHREPAIR_OT_fill_holes(MESHREPAIR_OT_Base):
    """Fill detected holes"""
    bl_idname = "meshrepair.fill_holes"
    bl_label = "Fill Holes"
    bl_description = "Fill all detected holes in the mesh"
    bl_options = {'UNDO'}

    def execute(self, context):
        if not self.check_engine():
            return {'CANCELLED'}

        if not self._check_edit_mode_selection(context):
            return {'CANCELLED'}

        prefs = get_prefs()
        props = context.scene.meshrepair_props
        obj = context.active_object

        start_time = time.time()
        session = None

        try:
            # Export mesh data (no temp files)
            self.report({'INFO'}, "Exporting mesh data...")
            export_result = self._export_active_mesh(context)
            mesh_data = export_result.mesh_data

            # Start engine session
            session = EngineSession(
                prefs.engine_path,
                verbosity=int(prefs.verbosity_level),
                socket_mode=prefs.use_socket_mode,
                socket_host=prefs.socket_host,
                socket_port=prefs.socket_port,
                temp_dir=prefs.temp_dir
            )

            # Load mesh via pipe (pass selection_info for boundary vertex handling)
            self.report({'INFO'}, "Loading mesh into engine...")
            session.load_mesh(mesh_data, selection_info=export_result if export_result.selection_only else None)

            # Build filling options
            max_boundary = props.filling_max_boundary
            max_diameter = props.filling_max_diameter_ratio
            options = {
                "continuity": int(props.filling_continuity),
                "refine": props.filling_refine,
                "refine_result": props.filling_refine,
                "use_2d_cdt": props.filling_use_2d_cdt,
                "use_3d_delaunay": props.filling_use_3d_delaunay,
                "skip_cubic": props.filling_skip_cubic,
                "skip_cubic_solver": props.filling_skip_cubic,
                "use_partitioned": props.filling_use_partitioned,
                "max_boundary": max_boundary,
                "max_hole_boundary_vertices": max_boundary,
                "max_diameter": max_diameter,
                "max_hole_diameter_ratio": max_diameter
            }

            # Fill holes
            self.console_report('INFO', "Filling holes...")
            response = session.fill_holes(options)

            # Get result and import via pipe
            self.console_report('INFO', "Receiving repaired mesh...")
            result_mesh_data = session.save_mesh()

            if session.batch_mode:
                resolved = session.get_last_response("fill_holes")
                if resolved:
                    response = resolved

            # Extract stats (stats is at root level)
            hole_stats = response.get('stats', {})

            props.last_hole_stats = True
            props.last_holes_detected = hole_stats.get('num_holes_detected', 0)
            props.last_holes_filled = hole_stats.get('num_holes_filled', 0)
            props.last_holes_failed = hole_stats.get('num_holes_failed', 0)
            props.last_holes_skipped = hole_stats.get('num_holes_skipped', 0)
            props.last_vertices_added = hole_stats.get('total_vertices_added', 0)
            props.last_faces_added = hole_stats.get('total_faces_added', 0)

            self.console_report('INFO', "Importing result...")
            import_mesh_from_data(
                result_mesh_data,
                obj,
                replace=not export_result.selection_only,
                selection_info=export_result if export_result.selection_only else None
            )

            # Update props
            elapsed_time = (time.time() - start_time) * 1000
            props.has_results = True
            props.last_operation_type = "Fill Holes"
            props.last_operation_status = "Success"
            props.last_operation_time_ms = elapsed_time

            msg = f"Filled {props.last_holes_filled}/{props.last_holes_detected} holes " \
                  f"({props.last_vertices_added} vertices, {props.last_faces_added} faces added)"
            self.console_report('INFO', msg)
            return {'FINISHED'}

        except Exception as ex:
            msg = f"Hole filling failed: {str(ex)}"
            self.console_report('ERROR', msg)
            props.last_operation_status = f"Error: {str(ex)}"
            return {'CANCELLED'}

        finally:
            if session:
                session.stop()


class MESHREPAIR_OT_repair_all(MESHREPAIR_OT_Base):
    """Complete repair pipeline with preset"""
    bl_idname = "meshrepair.repair_all"
    bl_label = "Repair All"
    bl_description = "Full repair pipeline with quality preset"
    bl_options = {'UNDO'}

    preset: EnumProperty(
        name="Preset",
        description="Quality preset",
        items=[
            ('FAST', "Fast", "C⁰ continuity, no refinement"),
            ('QUALITY', "Quality", "C¹ continuity, with refinement"),
            ('HIGH_QUALITY', "High Quality", "C² continuity, full refinement"),
        ],
        default='QUALITY'
    )

    def execute(self, context):
        if not self.check_engine():
            return {'CANCELLED'}

        if not self._check_edit_mode_selection(context):
            return {'CANCELLED'}

        prefs = get_prefs()
        props = context.scene.meshrepair_props
        obj = context.active_object

        # Apply preset settings
        if self.preset == 'FAST':
            props.filling_continuity = '0'
            props.filling_refine = False
            props.filling_skip_cubic = True
        elif self.preset == 'QUALITY':
            props.filling_continuity = '1'
            props.filling_refine = True
            props.filling_skip_cubic = False
        elif self.preset == 'HIGH_QUALITY':
            props.filling_continuity = '2'
            props.filling_refine = True
            props.filling_skip_cubic = False

        start_time = time.time()
        session = None

        try:
            # Export mesh data (no temp files)
            self.report({'INFO'}, "Exporting mesh data...")
            export_result = self._export_active_mesh(context)
            mesh_data = export_result.mesh_data

            # Start engine session
            session = EngineSession(
                prefs.engine_path,
                verbosity=int(prefs.verbosity_level),
                socket_mode=prefs.use_socket_mode,
                socket_host=prefs.socket_host,
                socket_port=prefs.socket_port,
                temp_dir=prefs.temp_dir
            )

            # Load mesh via pipe (pass selection_info for boundary vertex handling)
            self.report({'INFO'}, "Loading mesh into engine...")
            session.load_mesh(mesh_data, selection_info=export_result if export_result.selection_only else None)

            # Preprocessing (if enabled)
            preprocess_response = None
            if props.enable_preprocessing:
                preprocess_options = {
                    "remove_duplicates": props.preprocess_remove_duplicates,
                    "remove_non_manifold": props.preprocess_remove_non_manifold,
                    "remove_3_face_fans": props.preprocess_remove_3_face_fans,
                    "remove_isolated": props.preprocess_remove_isolated,
                    "keep_largest_component": props.preprocess_keep_largest,
                    "non_manifold_passes": props.preprocess_nm_passes,
                    "duplicate_threshold": props.preprocess_duplicate_threshold
                }

                self.console_report('INFO', "Preprocessing mesh...")
                preprocess_response = session.preprocess(preprocess_options)
            else:
                props.last_preprocess_stats = False

            # Build filling options
            max_boundary = props.filling_max_boundary
            max_diameter = props.filling_max_diameter_ratio
            filling_options = {
                "continuity": int(props.filling_continuity),
                "refine": props.filling_refine,
                "refine_result": props.filling_refine,
                "use_2d_cdt": props.filling_use_2d_cdt,
                "use_3d_delaunay": props.filling_use_3d_delaunay,
                "skip_cubic": props.filling_skip_cubic,
                "skip_cubic_solver": props.filling_skip_cubic,
                "use_partitioned": props.filling_use_partitioned,
                "max_boundary": max_boundary,
                "max_hole_boundary_vertices": max_boundary,
                "max_diameter": max_diameter,
                "max_hole_diameter_ratio": max_diameter
            }

            # Fill holes
            self.console_report('INFO', "Filling holes...")
            fill_response = session.fill_holes(filling_options)

            # Get result and import via pipe
            self.console_report('INFO', "Receiving repaired mesh...")
            result_mesh_data = session.save_mesh()

            if session.batch_mode:
                resolved_fill = session.get_last_response("fill_holes")
                if resolved_fill:
                    fill_response = resolved_fill
                if props.enable_preprocessing:
                    resolved_pre = session.get_last_response("preprocess")
                    if resolved_pre:
                        preprocess_response = resolved_pre

            self.console_report('INFO', "Importing result...")
            import_mesh_from_data(
                result_mesh_data,
                obj,
                replace=not export_result.selection_only,
                selection_info=export_result if export_result.selection_only else None
            )

            if props.enable_preprocessing and preprocess_response:
                preprocess_stats = preprocess_response.get('stats', {})
                props.last_preprocess_stats = True
                props.last_duplicate_count = preprocess_stats.get('duplicates_merged', 0)
                props.last_non_manifold_count = preprocess_stats.get('non_manifold_vertices_removed', 0)
                props.last_3_face_fan_count = preprocess_stats.get('face_fans_collapsed', 0)
                props.last_isolated_count = preprocess_stats.get('isolated_vertices_removed', 0)

            # Update props
            hole_stats = fill_response.get('stats', {})
            props.last_hole_stats = True
            props.last_holes_detected = hole_stats.get('num_holes_detected', 0)
            props.last_holes_filled = hole_stats.get('num_holes_filled', 0)
            props.last_holes_failed = hole_stats.get('num_holes_failed', 0)
            props.last_holes_skipped = hole_stats.get('num_holes_skipped', 0)
            props.last_vertices_added = hole_stats.get('total_vertices_added', 0)
            props.last_faces_added = hole_stats.get('total_faces_added', 0)

            elapsed_time = (time.time() - start_time) * 1000
            props.has_results = True
            props.last_operation_type = f"Repair All ({self.preset})"
            props.last_operation_status = "Success"
            props.last_operation_time_ms = elapsed_time

            msg = f"Repair complete with {self.preset} preset: " \
                  f"{props.last_holes_filled}/{props.last_holes_detected} holes filled in {elapsed_time:.1f}ms"
            self.console_report('INFO', msg)
            return {'FINISHED'}

        except Exception as ex:
            # Get full exception details including type
            import traceback
            exc_type = type(ex).__name__
            exc_msg = str(ex)
            exc_trace = traceback.format_exc()

            msg = f"Repair failed: [{exc_type}] {exc_msg}"
            self.console_report('ERROR', msg)
            props.last_operation_status = f"Error: {exc_msg}"

            # Print full traceback to console for debugging
            print(f"[MeshRepair ERROR] Exception traceback:", file=sys.stdout)
            print(exc_trace, file=sys.stdout)
            sys.stdout.flush()

            # Print log file location for debugging
            if session and hasattr(session, 'log_file_path') and session.log_file_path:
                self.console_report('ERROR', f"Check engine log: {session.log_file_path}")

            return {'CANCELLED'}

        finally:
            if session:
                session.stop()


classes = (
    MESHREPAIR_OT_preprocess,
    MESHREPAIR_OT_detect_holes,
    MESHREPAIR_OT_fill_holes,
    MESHREPAIR_OT_repair_all,
)


def register():
    for cls in classes:
        bpy.utils.register_class(cls)


def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
