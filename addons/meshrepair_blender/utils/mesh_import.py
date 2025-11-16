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


def import_mesh_from_data(mesh_data, target_obj, replace=True):
    """
    Import mesh from mesh soup data (vertices + faces arrays) into Blender.

    Args:
        mesh_data: Dict with 'vertices' and 'faces' keys
            vertices: List of [x, y, z] coords
            faces: List of [i0, i1, i2, ...] polygon indices
        target_obj: Target Blender object
        replace: Replace existing mesh data (True)

    Returns:
        bpy.types.Object: Updated object

    Raises:
        RuntimeError: If import fails
    """
    try:
        vertices = mesh_data['vertices']
        faces = mesh_data['faces']

        # Create bmesh from data
        bm = bmesh.new()

        # Add vertices
        bm_verts = []
        for v in vertices:
            bm_verts.append(bm.verts.new((v[0], v[1], v[2])))

        # Ensure lookup table
        bm.verts.ensure_lookup_table()

        # Add faces
        for face_indices in faces:
            try:
                face_verts = [bm_verts[i] for i in face_indices]
                bm.faces.new(face_verts)
            except (ValueError, IndexError):
                # Skip degenerate or invalid faces
                pass

        if replace:
            # Replace existing mesh data
            if target_obj.mode == 'EDIT':
                bmesh.update_edit_mesh(target_obj.data)
                bm_edit = bmesh.from_edit_mesh(target_obj.data)
                bm_edit.clear()

                # Copy data from bm to bm_edit
                for v in bm.verts:
                    bm_edit.verts.new(v.co)
                bm_edit.verts.ensure_lookup_table()

                for f in bm.faces:
                    face_verts = [bm_edit.verts[v.index] for v in f.verts]
                    try:
                        bm_edit.faces.new(face_verts)
                    except ValueError:
                        pass

                bmesh.update_edit_mesh(target_obj.data)
            else:
                bm.to_mesh(target_obj.data)
                target_obj.data.update()

        bm.free()

        return target_obj

    except Exception as ex:
        raise RuntimeError(f"Failed to import mesh: {ex}")
