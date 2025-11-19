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

        if replace:
            view_layer = bpy.context.view_layer
            previous_active = view_layer.objects.active
            previous_mode = target_obj.mode

            try:
                # Ensure target object is active and in object mode for safe data edits
                if previous_active is not target_obj:
                    view_layer.objects.active = target_obj
                previous_selection = target_obj.select_get()
                if not target_obj.select_get():
                    target_obj.select_set(True)
                if previous_mode != 'OBJECT':
                    bpy.ops.object.mode_set(mode='OBJECT')

                mesh = target_obj.data
                mesh.clear_geometry()
                mesh.from_pydata(vertices, [], faces)
                mesh.validate(verbose=False)
                mesh.update(calc_edges=True, calc_edges_loose=True)
            finally:
                # Restore original mode and active object
                if previous_mode != 'OBJECT':
                    bpy.ops.object.mode_set(mode=previous_mode)
                if 'previous_selection' in locals() and not previous_selection:
                    target_obj.select_set(False)
                if previous_active is not None and previous_active is not target_obj:
                    view_layer.objects.active = previous_active
        else:
            # Create new mesh datablock and assign to target object
            new_mesh = bpy.data.meshes.new(name=f"{target_obj.name}_repaired")
            new_mesh.from_pydata(vertices, [], faces)
            new_mesh.validate(verbose=False)
            new_mesh.update(calc_edges=True, calc_edges_loose=True)
            target_obj.data = new_mesh

        return target_obj

    except Exception as ex:
        raise RuntimeError(f"Failed to import mesh: {ex}")
