# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
# ##### END GPL LICENSE BLOCK #####

"""
Mesh export to mesh soup format

Converts Blender mesh to simple triangle soup for engine processing.
Exports directly to data structures (no temp files).
"""

import bpy
import bmesh
from dataclasses import dataclass
from typing import List, Optional, Set, Tuple


@dataclass
class MeshExportResult:
    """Container returned by export_mesh_to_data."""
    mesh_data: dict
    selection_only: bool
    selection_was_empty: bool
    vertex_orig_indices: List[int]
    boundary_vertex_flags: List[bool]
    face_orig_indices: List[int]
    object_bbox_diagonal: float = 0.0  # Bounding box diagonal of the FULL object (not selection)


def export_mesh_to_data(obj, selection_only=False, dilation_iters=0):
    """
    Export Blender mesh (or selection patch) to mesh soup data.

    Args:
        obj: Blender object (must be MESH type)
        selection_only: Limit export to active selection (edit mode)
        dilation_iters: Selection dilation iterations (>=0, ignored if not selection_only)

    Returns:
        MeshExportResult: Mesh soup data plus mapping metadata

    Raises:
        RuntimeError: If export fails
    """
    if obj.type != 'MESH':
        raise RuntimeError("Object must be MESH type")

    try:
        mesh = obj.data
        
        if obj.mode == 'EDIT':
            bm_edit = bmesh.from_edit_mesh(mesh)
            bm_edit.verts.ensure_lookup_table()
            bm_edit.faces.ensure_lookup_table()
            bm_edit.edges.ensure_lookup_table()
            
            bm = bmesh.new()
            bm_edit.verts.ensure_lookup_table()
            bm_edit.faces.ensure_lookup_table()
            
            vert_map = {}
            for bm_edit_vert in bm_edit.verts:
                bm_vert = bm.verts.new(bm_edit_vert.co)
                vert_map[bm_edit_vert] = bm_vert
            
            for bm_edit_face in bm_edit.faces:
                bm_verts = [vert_map[bm_edit_vert] for bm_edit_vert in bm_edit_face.verts]
                bm.faces.new(bm_verts)
            
            bm.verts.ensure_lookup_table()
            bm.faces.ensure_lookup_table()
            bm.edges.ensure_lookup_table()
            
            vert_layer = bm.verts.layers.int.new("meshrepair_orig_vert")
            face_layer = bm.faces.layers.int.new("meshrepair_orig_face")
            
            for bm_vert, bm_edit_vert in zip(bm.verts, bm_edit.verts):
                bm_vert[vert_layer] = bm_edit_vert.index
            
            for bm_face, bm_edit_face in zip(bm.faces, bm_edit.faces):
                bm_face[face_layer] = bm_edit_face.index
        else:
            bm = bmesh.new()
            bm.from_mesh(mesh)
            bm.faces.ensure_lookup_table()
            bm.edges.ensure_lookup_table()
            bm.verts.ensure_lookup_table()

            vert_layer = bm.verts.layers.int.new("meshrepair_orig_vert")
            face_layer = bm.faces.layers.int.new("meshrepair_orig_face")
            
            mesh_verts = mesh.vertices
            mesh_polys = mesh.polygons
            
            coord_to_mesh_idx = {}
            for i, mesh_vert in enumerate(mesh_verts):
                co = mesh_vert.co
                coord_key = (round(co.x, 6), round(co.y, 6), round(co.z, 6))
                coord_to_mesh_idx[coord_key] = i
            
            bm_vert_to_mesh_idx = {}
            for bm_vert in bm.verts:
                co = bm_vert.co
                coord_key = (round(co.x, 6), round(co.y, 6), round(co.z, 6))
                if coord_key in coord_to_mesh_idx:
                    mesh_idx = coord_to_mesh_idx[coord_key]
                    bm_vert[vert_layer] = mesh_idx
                    bm_vert_to_mesh_idx[bm_vert] = mesh_idx
                else:
                    bm_vert[vert_layer] = bm_vert.index
                    bm_vert_to_mesh_idx[bm_vert] = bm_vert.index
            
            # Build hash map: frozenset of vertex indices -> polygon index (O(n))
            poly_vert_hash = {}
            for poly_idx, poly in enumerate(mesh_polys):
                key = frozenset(poly.vertices)
                poly_vert_hash[key] = poly_idx

            # Match bmesh faces to polygon indices using hash lookup (O(n))
            for bm_face in bm.faces:
                bm_face_vert_indices = frozenset(
                    bm_vert_to_mesh_idx.get(bm_v, -1) for bm_v in bm_face.verts
                )
                if bm_face_vert_indices in poly_vert_hash:
                    bm_face[face_layer] = poly_vert_hash[bm_face_vert_indices]
                else:
                    bm_face[face_layer] = bm_face.index

        edit_selection = _collect_edit_mode_selection(obj) if obj.mode == 'EDIT' else None
        scoped_face_indices, selection_was_empty = _resolve_face_scope(
            bm,
            selection_only and obj.mode == 'EDIT',
            max(0, int(dilation_iters)),
            edit_selection,
            face_layer
        )

        boundary_vertex_indices = _compute_boundary_vertices(
            bm,
            scoped_face_indices,
            vert_layer,
            face_layer
        ) if scoped_face_indices else set()
        face_orig_indices = list(scoped_face_indices)

        if scoped_face_indices:
            _isolate_faces(bm, scoped_face_indices, face_layer)

        _triangulate_all(bm)

        mesh_data, vertex_orig_indices, boundary_flags = _serialize_bmesh(
            bm,
            boundary_vertex_indices,
            vert_layer
        )

        bm.free()

        actual_selection = bool(selection_only and obj.mode == 'EDIT' and not selection_was_empty)

        # Calculate object bounding box diagonal (for proper hole size ratio in selection mode)
        object_bbox_diagonal = _compute_object_bbox_diagonal(obj)

        return MeshExportResult(
            mesh_data=mesh_data,
            selection_only=actual_selection,
            selection_was_empty=selection_was_empty,
            vertex_orig_indices=vertex_orig_indices,
            boundary_vertex_flags=boundary_flags,
            face_orig_indices=face_orig_indices,
            object_bbox_diagonal=object_bbox_diagonal
        )

    except Exception as ex:
        raise RuntimeError(f"Failed to export mesh: {ex}")


def _collect_edit_mode_selection(obj):
    """Capture edit-mode selections as index sets."""
    bm = bmesh.from_edit_mesh(obj.data)
    faces = {face.index for face in bm.faces if face.select}
    edges = {edge.index for edge in bm.edges if edge.select}
    verts = {vert.index for vert in bm.verts if vert.select}
    return faces, edges, verts


def _resolve_face_scope(bm, selection_only, dilation_iters, edit_selection, face_layer):
    """
    Determine which faces to export.

    Returns:
        Tuple[Set[int], bool]: (face indices, selection_was_empty)
    """
    scoped_indices: Set[int]
    selection_was_empty = False

    if not selection_only or edit_selection is None:
        scoped_indices = {face[face_layer] for face in bm.faces}
        return scoped_indices, selection_was_empty

    selected_faces, selected_edges, selected_verts = edit_selection

    scoped_indices = set(selected_faces)
    if not scoped_indices and selected_edges:
        scoped_indices = {
            face[face_layer] for face in bm.faces
            if any(edge.index in selected_edges for edge in face.edges)
        }
    if not scoped_indices and selected_verts:
        scoped_indices = {
            face[face_layer] for face in bm.faces
            if any(vert.index in selected_verts for vert in face.verts)
        }

    if not scoped_indices:
        selection_was_empty = True
        scoped_indices = {face[face_layer] for face in bm.faces}
        return scoped_indices, selection_was_empty

    face_lookup = {face[face_layer]: face for face in bm.faces}
    scoped_face_objs = {face_lookup[idx] for idx in scoped_indices if idx in face_lookup}

    if dilation_iters > 0:
        scoped_face_objs = _dilate_face_set(scoped_face_objs, dilation_iters)

    scoped_indices = {face[face_layer] for face in scoped_face_objs}
    return scoped_indices, selection_was_empty


def _dilate_face_set(initial_faces, dilation_iters):
    """Expand a face set by traversing neighboring faces."""
    expanded = set(initial_faces)
    frontier = set(initial_faces)
    for _ in range(dilation_iters):
        next_frontier = set()
        for face in frontier:
            for edge in face.edges:
                for neighbor in edge.link_faces:
                    if neighbor not in expanded:
                        expanded.add(neighbor)
                        next_frontier.add(neighbor)
        if not next_frontier:
            break
        frontier = next_frontier
    return expanded


def _compute_object_bbox_diagonal(obj):
    """Compute the bounding box diagonal of the full object."""
    import math
    from mathutils import Vector

    # Use Blender's bound_box which gives 8 corners of the object-space bbox
    if hasattr(obj, 'bound_box') and obj.bound_box:
        corners = [obj.matrix_world @ Vector(obj.bound_box[i][:]) for i in range(8)]
        if corners:
            min_corner = [min(c[i] for c in corners) for i in range(3)]
            max_corner = [max(c[i] for c in corners) for i in range(3)]
            diagonal = math.sqrt(sum((max_corner[i] - min_corner[i])**2 for i in range(3)))
            return diagonal

    return 0.0


def _compute_boundary_vertices(bm, scoped_face_indices, vert_layer, face_layer):
    """Return original vertex indices that lie on the selection boundary."""
    scoped = set(scoped_face_indices)
    boundary = set()
    for vert in bm.verts:
        in_scope = False
        out_of_scope = False
        for face in vert.link_faces:
            face_idx = face[face_layer]
            if face_idx in scoped:
                in_scope = True
            else:
                out_of_scope = True
            if in_scope and out_of_scope:
                boundary.add(vert[vert_layer])
                break
    return boundary


def _isolate_faces(bm, scoped_face_indices, face_layer):
    """Delete faces outside the scoped set."""
    faces_to_delete = [face for face in bm.faces if face[face_layer] not in scoped_face_indices]
    if faces_to_delete:
        bmesh.ops.delete(bm, geom=faces_to_delete, context='FACES')
        bm.faces.ensure_lookup_table()
        bm.verts.ensure_lookup_table()


def _triangulate_all(bm):
    """Triangulate all faces in-place."""
    if bm.faces:
        bmesh.ops.triangulate(bm, faces=bm.faces[:])
        bm.faces.ensure_lookup_table()
        bm.verts.ensure_lookup_table()


def _serialize_bmesh(bm, boundary_vertex_indices, vert_layer):
    """Serialize bmesh into mesh soup plus metadata."""
    vertex_orig_indices = []
    boundary_flags = []
    vertices = []
    vertex_lookup = {}

    bm.verts.ensure_lookup_table()
    bm.faces.ensure_lookup_table()

    for vert in bm.verts:
        idx = len(vertices)
        vertex_lookup[vert] = idx
        vertices.append([vert.co.x, vert.co.y, vert.co.z])
        original_index = vert[vert_layer]
        vertex_orig_indices.append(original_index)
        boundary_flags.append(original_index in boundary_vertex_indices)

    faces = []
    for face in bm.faces:
        face_indices = [vertex_lookup[vert] for vert in face.verts]
        if len(face_indices) == 3:
            faces.append(face_indices)

    return (
        {
            'vertices': vertices,
            'faces': faces
        },
        vertex_orig_indices,
        boundary_flags
    )


def get_mesh_info(obj):
    """
    Get mesh statistics for logging.

    Args:
        obj: Blender object (MESH type)

    Returns:
        dict: Mesh info (vertices, faces, edges)
    """
    if obj.mode == 'EDIT':
        bm = bmesh.from_edit_mesh(obj.data)
        return {
            'vertices': len(bm.verts),
            'faces': len(bm.faces),
            'edges': len(bm.edges)
        }
    else:
        mesh = obj.data
        return {
            'vertices': len(mesh.vertices),
            'faces': len(mesh.polygons),
            'edges': len(mesh.edges)
        }
