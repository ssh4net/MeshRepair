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


def export_mesh_to_data(obj, selection_only=False):
    """
    Export Blender mesh to mesh soup data (vertices + faces arrays).

    Args:
        obj: Blender object (must be MESH type)
        selection_only: Export only selected faces (Edit mode)

    Returns:
        dict: Mesh data with 'vertices' and 'faces' keys
            vertices: List of [x, y, z] coords
            faces: List of [i0, i1, i2] triangle indices

    Raises:
        RuntimeError: If export fails
    """
    if obj.type != 'MESH':
        raise RuntimeError("Object must be MESH type")

    try:
        # Get mesh data
        if obj.mode == 'EDIT':
            bm = bmesh.from_edit_mesh(obj.data)
            should_free = False
        else:
            bm = bmesh.new()
            bm.from_mesh(obj.data)
            should_free = True

        # Triangulate (engine expects triangles)
        bmesh.ops.triangulate(bm, faces=bm.faces)

        # Build vertex map for selection
        vertex_map = {}
        vertices = []

        for vert in bm.verts:
            if selection_only and not vert.select:
                continue
            vertex_map[vert.index] = len(vertices)
            vertices.append([vert.co.x, vert.co.y, vert.co.z])

        # Build face list
        faces = []
        for face in bm.faces:
            if selection_only and not face.select:
                continue

            # Map vertex indices
            face_indices = []
            valid = True
            for vert in face.verts:
                if vert.index in vertex_map:
                    face_indices.append(vertex_map[vert.index])
                else:
                    valid = False
                    break

            if valid and len(face_indices) == 3:  # Should always be 3 after triangulation
                faces.append(face_indices)

        # Clean up bmesh if we created it
        if should_free:
            bm.free()

        return {
            'vertices': vertices,
            'faces': faces
        }

    except Exception as ex:
        raise RuntimeError(f"Failed to export mesh: {ex}")


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
