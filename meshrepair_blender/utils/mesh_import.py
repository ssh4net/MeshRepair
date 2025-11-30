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

            # If selection mode, remap orig indices/boundary flags to the returned mesh order
            if selection_patch:
                _remap_selection_metadata(mesh_data, selection_info)

            if replace or not selection_patch:
                _replace_mesh_fast(mesh, vertices, faces)
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


def _replace_mesh_fast(mesh, vertices, faces_tri):
    """Fast full-mesh replace using Mesh foreach_set (no BMesh/from_pydata)."""
    mesh.clear_geometry()

    vertices_seq = list(vertices)
    faces_seq = [list(f) for f in faces_tri]

    # Filter out degenerate faces and guard against empty results.
    faces_seq = [f for f in faces_seq if len(f) >= 3]

    vertex_count = len(vertices_seq)
    poly_count = len(faces_seq)

    if vertex_count == 0 or poly_count == 0:
        mesh.update(calc_edges=True, calc_edges_loose=True)
        return

    # Support arbitrary polygon sizes (engine typically returns tris/quads).
    loop_totals = [len(face) for face in faces_seq]
    loop_count = sum(loop_totals)

    mesh.vertices.add(vertex_count)
    mesh.loops.add(loop_count)
    mesh.polygons.add(poly_count)

    flat_coords = [coord for v in vertices_seq for coord in v]
    mesh.vertices.foreach_set("co", flat_coords)

    loop_vertex_index = [idx for face in faces_seq for idx in face]
    loop_start = []
    running = 0
    for total in loop_totals:
        loop_start.append(running)
        running += total

    mesh.loops.foreach_set("vertex_index", loop_vertex_index)
    mesh.polygons.foreach_set("loop_start", loop_start)
    mesh.polygons.foreach_set("loop_total", loop_totals)

    mesh.validate(verbose=False)
    mesh.update(calc_edges=True, calc_edges_loose=True)

    # Fallback: if no polygons were created (Blender rejected data), rebuild via from_pydata.
    if len(mesh.polygons) == 0 and faces_seq:
        mesh.clear_geometry()
        mesh.from_pydata(vertices_seq, [], faces_seq)
        mesh.validate(verbose=False)
        mesh.update(calc_edges=True, calc_edges_loose=True)


def _patch_selection(mesh_data, target_obj, selection_info: MeshExportResult):
    """Merge repaired selection patch back into the original mesh."""
    if not selection_info or not selection_info.selection_only:
        raise RuntimeError("Selection patch requested without selection metadata")

    mesh = target_obj.data
    bm = bmesh.new()
    bm_freed = False
    try:
        bm.from_mesh(mesh)
        bm.verts.ensure_lookup_table()
        bm.faces.ensure_lookup_table()

        vert_layer = bm.verts.layers.int.get("meshrepair_orig_vert")
        if vert_layer is None:
            vert_layer = bm.verts.layers.int.new("meshrepair_orig_vert")
        # Always refresh to current indexing to avoid stale mappings after user edits
        for vert in bm.verts:
            vert[vert_layer] = vert.index

        face_layer = bm.faces.layers.int.get("meshrepair_orig_face")
        if face_layer is None:
            face_layer = bm.faces.layers.int.new("meshrepair_orig_face")
            bm.faces.ensure_lookup_table()
        # Always refresh to current indexing to avoid stale mappings after user edits
        for face in bm.faces:
            face[face_layer] = face.index

        # Session tagging to isolate selections between runs
        session_layer = bm.faces.layers.int.get("meshrepair_session_id")
        if session_layer is None:
            session_layer = bm.faces.layers.int.new("meshrepair_session_id")
        # Use a bounded session id that fits into Blender's int custom data (C int)
        import time
        current_session_id = int(time.time() * 1000) & 0x7FFFFFFF

        faces_to_remove = set(getattr(selection_info, "faces_to_delete", []) or [])
        if selection_info.selection_only and not getattr(selection_info, "remesh_selection", False):
            faces_to_remove.clear()  # Selection mode: keep original faces, only add filled holes
        elif not faces_to_remove:
            faces_to_remove = set(selection_info.face_orig_indices)

        if faces_to_remove:
            faces_to_delete = [face for face in bm.faces if face[face_layer] in faces_to_remove]
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

        from mathutils import kdtree, Vector
        kd = None
        if selection_info.selection_only and bm.verts:
            kd = kdtree.KDTree(len(bm.verts))
            for i, v in enumerate(bm.verts):
                kd.insert(v.co, i)
            kd.balance()
            # scale-aware tiny epsilon
            bbox_min = Vector((min(v.co.x for v in bm.verts),
                               min(v.co.y for v in bm.verts),
                               min(v.co.z for v in bm.verts)))
            bbox_max = Vector((max(v.co.x for v in bm.verts),
                               max(v.co.y for v in bm.verts),
                               max(v.co.z for v in bm.verts)))
            bbox_diag = (bbox_max - bbox_min).length
            reuse_epsilon = max(1e-6, bbox_diag * 1e-8)
        else:
            reuse_epsilon = None

        for local_idx, coord in enumerate(source_vertices):
            # Vertices beyond orig_count are new vertices added by the engine (hole filling)
            if local_idx < orig_count:
                orig_idx = vertex_orig_indices[local_idx]
                is_boundary = boundary_flags[local_idx]
            else:
                # New vertex added by engine - no original mapping
                orig_idx = -1
                is_boundary = False

            if is_boundary and orig_idx in index_lookup:
                vertex_map[local_idx] = index_lookup[orig_idx]
                continue

            if is_boundary and kd is not None:
                co_vec = Vector(coord)
                _, idx, dist = kd.find(co_vec)
                bm.verts.ensure_lookup_table()
                if dist <= reuse_epsilon and idx < len(bm.verts):
                    match_vert = bm.verts[idx]
                    if match_vert.is_valid:
                        vertex_map[local_idx] = match_vert
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

        # Weld only in full-mesh replace; for selection patches, keep seam intact
        if not selection_info.selection_only:
            bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-6)

        # Clear selection then select only newly created faces (face_layer == -1) for this session
        for f in bm.faces:
            f.select = False
        for e in bm.edges:
            e.select = False
        for v in bm.verts:
            v.select = False
        new_faces = []
        if face_layer is not None:
            for f in bm.faces:
                if f.is_valid and f[face_layer] == -1:
                    f[session_layer] = current_session_id
                    new_faces.append(f)
                    f.select = True
                    for e in f.edges:
                        if e.is_valid:
                            e.select = True
                    for v in f.verts:
                        if v.is_valid:
                            v.select = True
        # Normalize layer indices to avoid stale -1 across runs
        bm.faces.ensure_lookup_table()
        bm.verts.ensure_lookup_table()
        for idx, face in enumerate(bm.faces):
            face[face_layer] = idx
        for idx, vert in enumerate(bm.verts):
            vert[vert_layer] = idx
        # Drop session layer to avoid accumulation between runs
        try:
            bm.faces.layers.int.remove(session_layer)
        except Exception:
            pass
        bm.normal_update()
        bm.to_mesh(mesh)
        mesh.validate(verbose=False)
        mesh.update(calc_edges=True, calc_edges_loose=True)
    finally:
        try:
            vertex_map.clear()
        except Exception:
            pass
        try:
            index_lookup.clear()
        except Exception:
            pass
        bm_freed = True
        try:
            bm.free()
        except Exception:
            pass


def _remap_selection_metadata(result_mesh_data, selection_info: MeshExportResult):
    """
    Remap vertex_orig_indices and boundary flags to the engine-returned mesh order
    by matching coordinates against the originally exported mesh.
    """
    if not selection_info or not selection_info.selection_only:
        return

    orig_vertices = selection_info.mesh_data.get('vertices', [])
    orig_indices = selection_info.vertex_orig_indices
    orig_boundary = selection_info.boundary_vertex_flags
    if not orig_vertices or not orig_indices or len(orig_indices) != len(orig_vertices):
        return

    def key_from_coord(coord):
        return (round(coord[0], 6), round(coord[1], 6), round(coord[2], 6))

    lookup = {}
    for idx, coord in enumerate(orig_vertices):
        lookup[key_from_coord(coord)] = (orig_indices[idx], orig_boundary[idx])

    new_orig_indices = []
    new_boundary = []
    for coord in result_mesh_data.get('vertices', []):
        key = key_from_coord(coord)
        if key in lookup:
            orig_idx, is_boundary = lookup[key]
        else:
            orig_idx, is_boundary = -1, False
        new_orig_indices.append(orig_idx)
        new_boundary.append(is_boundary)

    selection_info.vertex_orig_indices = new_orig_indices
    selection_info.boundary_vertex_flags = new_boundary
