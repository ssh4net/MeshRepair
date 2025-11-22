# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
# ##### END GPL LICENSE BLOCK #####

"""
Mesh import from mesh soup data

Imports repaired mesh back into Blender.
Direct data import (no temp files).
"""

import bpy
import bmesh
from .mesh_export import MeshExportResult


def import_mesh_from_data(mesh_data, target_obj, replace=True, selection_info: MeshExportResult = None):
    """
    Import mesh from mesh soup data (vertices + faces arrays) into Blender.

    Args:
        mesh_data: Dict with 'vertices' and 'faces' keys
            vertices: List of [x, y, z] coords
            faces: List of [i0, i1, i2, ...] polygon indices
        target_obj: Target Blender object
        replace: Replace existing mesh data (True)
        selection_info: MeshExportResult metadata when merging selection patches

    Returns:
        bpy.types.Object: Updated object

    Raises:
        RuntimeError: If import fails
    """
    try:
        vertices = mesh_data['vertices']
        faces = mesh_data['faces']

        selection_patch = selection_info.selection_only if selection_info else False

        view_layer = bpy.context.view_layer
        previous_active = view_layer.objects.active
        previous_mode = target_obj.mode
        previous_selection = target_obj.select_get()

        try:
            # Ensure target object is active and in object mode for safe data edits
            if previous_active is not target_obj:
                view_layer.objects.active = target_obj
            if not target_obj.select_get():
                target_obj.select_set(True)
            if previous_mode != 'OBJECT':
                bpy.ops.object.mode_set(mode='OBJECT')

            mesh = target_obj.data

            if replace or not selection_patch:
                mesh.clear_geometry()
                mesh.from_pydata(vertices, [], faces)
                mesh.validate(verbose=False)
                mesh.update(calc_edges=True, calc_edges_loose=True)
            else:
                _patch_selection(mesh_data, target_obj, selection_info)
        finally:
            # Restore original mode and active object
            if previous_mode != 'OBJECT':
                bpy.ops.object.mode_set(mode=previous_mode)
            if not previous_selection:
                target_obj.select_set(False)
            if previous_active is not None and previous_active is not target_obj:
                view_layer.objects.active = previous_active

        return target_obj

    except Exception as ex:
        raise RuntimeError(f"Failed to import mesh: {ex}")


def _patch_selection(mesh_data, target_obj, selection_info: MeshExportResult):
    """Merge repaired selection patch back into the original mesh."""
    if not selection_info or not selection_info.selection_only:
        raise RuntimeError("Selection patch requested without selection metadata")

    mesh = target_obj.data
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bm.verts.ensure_lookup_table()
    bm.faces.ensure_lookup_table()

    vert_layer = bm.verts.layers.int.get("meshrepair_orig_vert")
    if vert_layer is None:
        vert_layer = bm.verts.layers.int.new("meshrepair_orig_vert")
        for vert in bm.verts:
            vert[vert_layer] = vert.index

    face_layer = bm.faces.layers.int.get("meshrepair_orig_face")
    if face_layer is None:
        face_layer = bm.faces.layers.int.new("meshrepair_orig_face")
        bm.faces.ensure_lookup_table()
        for face in bm.faces:
            face[face_layer] = face.index

    face_indices = set(selection_info.face_orig_indices)
    if face_indices:
        faces_to_delete = [face for face in bm.faces if face[face_layer] in face_indices]
        if faces_to_delete:
            bmesh.ops.delete(bm, geom=faces_to_delete, context='FACES')

    bm.verts.ensure_lookup_table()
    bm.faces.ensure_lookup_table()

    index_lookup = {vert[vert_layer]: vert for vert in bm.verts}
    vertex_map = {}

    source_vertices = mesh_data['vertices']
    boundary_flags = selection_info.boundary_vertex_flags
    vertex_orig_indices = selection_info.vertex_orig_indices
    orig_count = len(vertex_orig_indices)

    for local_idx, coord in enumerate(source_vertices):
        # Vertices beyond orig_count are new vertices added by the engine (hole filling)
        if local_idx < orig_count:
            orig_idx = vertex_orig_indices[local_idx]
            is_boundary = boundary_flags[local_idx]
        else:
            # New vertex added by engine - no original mapping
            orig_idx = -1
            is_boundary = False

        reuse_existing = is_boundary and orig_idx in index_lookup

        if reuse_existing:
            vertex_map[local_idx] = index_lookup[orig_idx]
            continue

        vert = bm.verts.new(coord)
        vert[vert_layer] = orig_idx if orig_idx >= 0 else -1
        vertex_map[local_idx] = vert

    bm.verts.ensure_lookup_table()

    for face_indices in mesh_data['faces']:
        verts = [vertex_map[idx] for idx in face_indices]
        try:
            new_face = bm.faces.new(verts)
        except ValueError:
            new_face = bm.faces.get(verts)
            if new_face is None:
                raise
        if new_face and face_layer is not None:
            new_face[face_layer] = -1

    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)
    bm.normal_update()
    bm.to_mesh(mesh)
    mesh.validate(verbose=False)
    mesh.update(calc_edges=True, calc_edges_loose=True)
    bm.free()
