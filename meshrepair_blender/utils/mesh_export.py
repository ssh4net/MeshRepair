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
from dataclasses import dataclass, field
from typing import List, Set, Tuple


@dataclass
class MeshExportResult:
    """Container returned by export_mesh_to_data."""
    mesh_data: dict
    selection_only: bool
    selection_was_empty: bool
    selection_hole_count: int
    vertex_orig_indices: List[int]
    boundary_vertex_flags: List[bool]  # Rim boundary flags (hole rim for Selection, user rim for Remesh)
    engine_boundary_indices: List[int]  # Guard boundary indices (expanded selection border)
    face_orig_indices: List[int]
    object_bbox_diagonal: float = 0.0  # Bounding box diagonal of the FULL object (not selection)
    faces_to_delete: List[int] = field(default_factory=list)  # Original faces to remove before importing result
    remesh_selection: bool = False  # Whether selection was treated as a hole boundary


def _export_mesh_full_fast(obj):
    """Fast object-mode export using Mesh loop triangles and foreach_get."""
    mesh = obj.data.copy()
    try:
        mesh.calc_loop_triangles()

        vertex_count = len(mesh.vertices)
        tri_count = len(mesh.loop_triangles)

        coords = [0.0] * (vertex_count * 3)
        mesh.vertices.foreach_get("co", coords)

        tri_flat = [0] * (tri_count * 3)
        mesh.loop_triangles.foreach_get("vertices", tri_flat)

        vertices = [coords[i:i + 3] for i in range(0, len(coords), 3)]
        faces = [tri_flat[i:i + 3] for i in range(0, len(tri_flat), 3)]

        object_bbox_diagonal = _compute_object_bbox_diagonal(obj)

        return MeshExportResult(
            mesh_data={
                'vertices': vertices,
                'faces': faces
            },
            selection_only=False,
            selection_was_empty=False,
            selection_hole_count=0,
            vertex_orig_indices=list(range(vertex_count)),
            boundary_vertex_flags=[False] * vertex_count,
            engine_boundary_indices=[],
            face_orig_indices=list(range(tri_count)),
            object_bbox_diagonal=object_bbox_diagonal,
            faces_to_delete=[],
            remesh_selection=False
        )
    finally:
        try:
            bpy.data.meshes.remove(mesh)
        except Exception:
            pass


def export_mesh_to_data(obj, selection_only=False, dilation_iters=0, remesh_selection=False):
    """
    Export Blender mesh (or selection patch) to mesh soup data.

    Args:
        obj: Blender object (must be MESH type)
        selection_only: Limit export to active selection (edit mode)
        dilation_iters: Selection dilation iterations (>=0, ignored if not selection_only)
        remesh_selection: Treat the selection as a hole boundary (selected faces removed, dilated ring exported)

    Returns:
        MeshExportResult: Mesh soup data plus mapping metadata

    Raises:
        RuntimeError: If export fails
    """
    if obj.type != 'MESH':
        raise RuntimeError("Object must be MESH type")

    # Fast path for full-mesh/object-mode export: use Mesh foreach_get/loop_triangles.
    if obj.mode != 'EDIT' and not selection_only:
        return _export_mesh_full_fast(obj)

    bm = None
    bm_freed = False

    try:
        mesh = obj.data
        select_mode = None
        if obj.mode == 'EDIT':
            # Keep track of the active select mode so vertex-only selections can be mapped to faces.
            try:
                select_mode = tuple(bpy.context.tool_settings.mesh_select_mode)
            except Exception:
                select_mode = None
        
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

        base_selection = _collect_edit_mode_selection(obj) if obj.mode == 'EDIT' else None
        edit_selection = base_selection
        dilation_iters = max(0, int(dilation_iters))
        if remesh_selection and selection_only:
            dilation_iters = max(1, dilation_iters)
        if selection_only and dilation_iters <= 0:
            dilation_iters = 1  # Always at least one polygon ring in selection modes

        # Expand selection using Blender's face-step select_more; treat expansion as the scoped patch,
        # but keep the base selection separately so we can delete only those faces in Remesh.
        expanded = None
        if selection_only and obj.mode == 'EDIT':
            expanded = _expand_edit_selection_face_step(obj, dilation_iters)
            if expanded and expanded[0]:
                edit_selection = expanded
                dilation_iters = 0

        scoped_face_indices, selection_was_empty, selected_face_indices = _resolve_face_scope(
            bm,
            selection_only and obj.mode == 'EDIT',
            dilation_iters,
            edit_selection,
            face_layer,
            select_mode
        )

        if selection_was_empty:
            remesh_selection = False

        faces_to_delete = set()
        export_face_indices = set(scoped_face_indices)
        selection_hole_count = 0

        if selection_only and not selection_was_empty:
            if remesh_selection:
                base_selected_faces = set(base_selection[0]) if base_selection else set()
                faces_to_delete = base_selected_faces if base_selected_faces else set(selected_face_indices)
                # For remesh we export only the expanded patch (selection scope).
                export_face_indices = set(scoped_face_indices)
            else:
                # Selection mode: keep original faces in Blender, just measure holes
                faces_to_delete = set()
                selection_hole_count = _count_scoped_holes(bm, export_face_indices, face_layer)

        # Avoid empty exports when remeshing covers the whole scoped region
        if remesh_selection and selection_only and not export_face_indices and scoped_face_indices:
            export_face_indices = set(scoped_face_indices)
            faces_to_delete = set(export_face_indices)
            remesh_selection = False

        # Guard: selection border (expanded). Used only by the engine to avoid filling across selection edge.
        guard_face_indices = set(expanded[0]) if expanded and expanded[0] else export_face_indices
        topology_boundary_vertices = _compute_topology_boundary_vertices(bm, vert_layer)
        guard_vertex_indices = _compute_boundary_vertices(
            bm,
            guard_face_indices,
            vert_layer,
            face_layer,
            ignored_faces=faces_to_delete if remesh_selection else None
        ) if guard_face_indices else set()
        guard_vertex_indices = guard_vertex_indices - topology_boundary_vertices

        # Rim: actual hole border to be stitched on import.
        rim_vertex_indices: Set[int] = set()
        if selection_only and not selection_was_empty:
            if remesh_selection and faces_to_delete:
                rim_vertex_indices = _compute_face_set_boundary_vertices(
                    bm,
                    faces_to_delete,
                    face_layer,
                    vert_layer
                )
            elif not remesh_selection:
                rim_vertex_indices = _compute_hole_boundary_vertices(
                    bm,
                    export_face_indices,
                    face_layer,
                    vert_layer
                )

        engine_boundary_indices = guard_vertex_indices
        face_orig_indices = list(export_face_indices)
        faces_to_delete_list = list(faces_to_delete) if faces_to_delete else []
        engine_boundary_indices_list = []

        if faces_to_delete and remesh_selection:
            faces_to_delete_geom = [face for face in bm.faces if face[face_layer] in faces_to_delete]
            if faces_to_delete_geom:
                bmesh.ops.delete(bm, geom=faces_to_delete_geom, context='FACES')
                bm.faces.ensure_lookup_table()
                bm.verts.ensure_lookup_table()

        if export_face_indices:
            _isolate_faces(bm, export_face_indices, face_layer)
            _remove_isolated_vertices(bm)

        _triangulate_all(bm)

        mesh_data, vertex_orig_indices, boundary_flags = _serialize_bmesh(
            bm,
            rim_vertex_indices,
            vert_layer
        )

        # Map guard boundary (selection border) to exported vertex indices expected by the engine
        if engine_boundary_indices:
            guard_lookup = set(engine_boundary_indices)
            engine_boundary_indices_list = [
                idx for idx, orig in enumerate(vertex_orig_indices) if orig in guard_lookup
            ]

        bm.free()
        bm_freed = True

        # In remesh mode we export the full mesh (with selected faces removed), so import should replace.
        actual_selection = bool(selection_only and obj.mode == 'EDIT' and not selection_was_empty)
        remesh_flag = bool(remesh_selection and not selection_was_empty)

        # Calculate object bounding box diagonal (for proper hole size ratio in selection mode)
        object_bbox_diagonal = _compute_object_bbox_diagonal(obj)

        return MeshExportResult(
            mesh_data=mesh_data,
            selection_only=actual_selection,
            selection_was_empty=selection_was_empty,
            selection_hole_count=selection_hole_count,
            vertex_orig_indices=vertex_orig_indices,
            boundary_vertex_flags=boundary_flags,
            engine_boundary_indices=engine_boundary_indices_list,
            face_orig_indices=face_orig_indices,
            object_bbox_diagonal=object_bbox_diagonal,
            faces_to_delete=faces_to_delete_list,
            remesh_selection=remesh_flag
        )

    except Exception as ex:
        raise RuntimeError(f"Failed to export mesh: {ex}")
    finally:
        if bm is not None and not bm_freed:
            try:
                bm.free()
            except Exception:
                pass


def _collect_edit_mode_selection(obj):
    """Capture edit-mode selections as index sets."""
    bm = bmesh.from_edit_mesh(obj.data)
    faces = {face.index for face in bm.faces if face.select}
    edges = set()
    verts = set()
    return faces, edges, verts


def _expand_edit_selection_face_step(obj, iterations):
    """Expand the current edit-mode selection using Blender's face-step select_more (API-compatible)."""
    if not obj or obj.mode != 'EDIT' or iterations <= 0:
        return None

    for _ in range(iterations):
        try:
            bpy.ops.mesh.select_more(use_face_step=True)
        except Exception:
            return None

    bm = bmesh.from_edit_mesh(obj.data)
    bm.faces.ensure_lookup_table()
    bm.edges.ensure_lookup_table()
    bm.verts.ensure_lookup_table()

    selected_faces = {face.index for face in bm.faces if face.select}
    selected_edges = {edge.index for edge in bm.edges if edge.select}
    selected_verts = {vert.index for vert in bm.verts if vert.select}

    # After expanding, rely only on face selection for exporting.
    if not selected_faces:
        return None

    return (
        selected_faces,
        selected_edges,
        selected_verts
    )


def _safe_update_edit_mesh(mesh):
    """Call bmesh.update_edit_mesh with version-safe signature."""
    try:
        bmesh.update_edit_mesh(mesh, loop_triangles=False, destructive=False)
    except TypeError:
        try:
            bmesh.update_edit_mesh(mesh, tessface=False, destructive=False)
        except TypeError:
            bmesh.update_edit_mesh(mesh)


def _resolve_face_scope(bm, selection_only, dilation_iters, edit_selection, face_layer, select_mode=None):
    """
    Determine which faces to export.

    Returns:
        Tuple[Set[int], bool, Set[int]]: (face indices, selection_was_empty, directly selected faces)
    """
    scoped_indices: Set[int]
    selected_indices: Set[int] = set()
    selection_was_empty = False

    if not selection_only or edit_selection is None:
        scoped_indices = {face[face_layer] for face in bm.faces}
        return scoped_indices, selection_was_empty, selected_indices

    selected_faces, _, _ = edit_selection

    face_lookup = {face[face_layer]: face for face in bm.faces}

    selected_indices = set(selected_faces)

    if not selected_indices:
        selection_was_empty = True
        scoped_indices = {face[face_layer] for face in bm.faces}
        return scoped_indices, selection_was_empty, set()

    selected_face_objs = {face_lookup[idx] for idx in selected_indices if idx in face_lookup}
    scoped_face_objs = set(selected_face_objs)

    if dilation_iters > 0:
        scoped_face_objs = _dilate_face_set(scoped_face_objs, dilation_iters)

    scoped_indices = {face[face_layer] for face in scoped_face_objs}
    selected_indices = {face[face_layer] for face in selected_face_objs}
    return scoped_indices, selection_was_empty, selected_indices


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


def _compute_boundary_vertices(bm, scoped_face_indices, vert_layer, face_layer, ignored_faces=None):
    """Return original vertex indices that lie on the selection boundary (faces in vs out of scope)."""
    scoped = set(scoped_face_indices)
    ignored = set(ignored_faces) if ignored_faces else set()
    boundary = set()
    for edge in bm.edges:
        linked = edge.link_faces
        if len(linked) < 1:
            continue
        in_scope = False
        out_scope = False
        for face in linked:
            f_idx = face[face_layer]
            if f_idx in ignored:
                continue
            if f_idx in scoped:
                in_scope = True
            else:
                out_scope = True
        if in_scope and out_scope:
            for vert in edge.verts:
                boundary.add(vert[vert_layer])
    return boundary


def _compute_hole_boundary_vertices(bm, scoped_face_indices, face_layer, vert_layer):
    """
    Return original vertex indices on real hole borders within the scoped faces (edges with a single in-scope face).
    """
    scoped = set(scoped_face_indices)
    boundary_edges = []
    for edge in bm.edges:
        linked_in_scope = [face for face in edge.link_faces if face[face_layer] in scoped]
        if len(edge.link_faces) == 1 and len(linked_in_scope) == 1:
            boundary_edges.append(edge)

    boundary = set()
    for edge in boundary_edges:
        for vert in edge.verts:
            boundary.add(vert[vert_layer])
    return boundary


def _compute_face_set_boundary_vertices(bm, face_indices, face_layer, vert_layer):
    """
    Return original vertex indices on the boundary of a face set (adjacent to non-set faces or open edges).
    Used to capture the user-selected rim before expansion/deletion in remesh mode.
    """
    face_ids = set(face_indices)
    boundary = set()
    for edge in bm.edges:
        linked = edge.link_faces
        if not linked:
            continue
        in_count = 0
        for face in linked:
            if face[face_layer] in face_ids:
                in_count += 1
        if in_count == 0:
            continue
        out_count = len(linked) - in_count
        if out_count > 0 or len(linked) < 2:
            for vert in edge.verts:
                boundary.add(vert[vert_layer])
    return boundary


def _compute_topology_boundary_vertices(bm, vert_layer):
    """Return original vertex indices that lie on true mesh boundaries (missing adjacent faces)."""
    boundary = set()
    for edge in bm.edges:
        if len(edge.link_faces) < 2:
            for vert in edge.verts:
                boundary.add(vert[vert_layer])
    return boundary


def _isolate_faces(bm, scoped_face_indices, face_layer):
    """Delete faces outside the scoped set."""
    faces_to_delete = [face for face in bm.faces if face[face_layer] not in scoped_face_indices]
    if faces_to_delete:
        bmesh.ops.delete(bm, geom=faces_to_delete, context='FACES')
        bm.faces.ensure_lookup_table()
        bm.verts.ensure_lookup_table()


def _remove_isolated_vertices(bm):
    """Remove vertices that are no longer referenced by any face."""
    unused = [vert for vert in bm.verts if not vert.link_faces]
    if unused:
        bmesh.ops.delete(bm, geom=unused, context='VERTS')
        bm.verts.ensure_lookup_table()


def _triangulate_all(bm):
    """Triangulate all faces in-place."""
    if bm.faces:
        bmesh.ops.triangulate(bm, faces=bm.faces[:])
        bm.faces.ensure_lookup_table()
        bm.verts.ensure_lookup_table()


def _count_scoped_holes(bm, scoped_face_indices, face_layer):
    """
    Count boundary edge loops that belong to the scoped faces (real holes inside the selection).
    Edges shared with out-of-scope faces are ignored so selection borders are not treated as holes.
    """
    scoped = set(scoped_face_indices)
    boundary_edges = []
    for edge in bm.edges:
        linked_in_scope = [face for face in edge.link_faces if face[face_layer] in scoped]
        if len(edge.link_faces) == 1 and len(linked_in_scope) == 1:
            boundary_edges.append(edge)

    if not boundary_edges:
        return 0

    vert_to_edges = {}
    for edge in boundary_edges:
        for vert in edge.verts:
            vert_to_edges.setdefault(vert, set()).add(edge)

    visited = set()
    loops = 0
    for edge in boundary_edges:
        if edge in visited:
            continue
        loops += 1
        stack = [edge]
        while stack:
            e = stack.pop()
            if e in visited:
                continue
            visited.add(e)
            for vert in e.verts:
                for neighbor in vert_to_edges.get(vert, []):
                    if neighbor not in visited:
                        stack.append(neighbor)
    return loops


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
