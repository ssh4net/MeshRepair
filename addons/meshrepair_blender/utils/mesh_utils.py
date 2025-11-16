# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
# ##### END GPL LICENSE BLOCK #####

"""
Mesh import/export utilities

Handles conversion between Blender BMesh and temporary files.
"""

import bpy
import bmesh
import tempfile
import os


def export_mesh(obj, file_format='OBJ', selection_only=False):
    """
    Export Blender mesh to temporary file.

    Args:
        obj: Blender object (must be MESH type)
        file_format: 'OBJ' or 'PLY'
        selection_only: Export only selected faces (Edit mode)

    Returns:
        str: Path to temporary file

    Raises:
        RuntimeError: If export fails
    """
    # Determine file extension
    if file_format == 'OBJ':
        ext = '.obj'
    elif file_format == 'PLY':
        ext = '.ply'
    else:
        raise ValueError(f"Unsupported format: {file_format}")

    # Create temporary file
    temp_file = tempfile.mktemp(suffix=ext, prefix="meshrepair_")

    try:
        # Export based on format
        if file_format == 'OBJ':
            bpy.ops.wm.obj_export(
                filepath=temp_file,
                export_selected_objects=True,
                export_uv=False,
                export_normals=False,
                export_materials=False
            )
        elif file_format == 'PLY':
            bpy.ops.wm.ply_export(
                filepath=temp_file,
                export_selected_objects=True,
                export_uv=False,
                export_normals=False,
                export_colors='NONE'
            )

        return temp_file

    except Exception as ex:
        # Clean up on failure
        if os.path.exists(temp_file):
            os.remove(temp_file)
        raise RuntimeError(f"Failed to export mesh: {ex}")


def import_mesh(filepath, target_obj=None, replace=False):
    """
    Import mesh from file back into Blender.

    Args:
        filepath: Path to mesh file (OBJ or PLY)
        target_obj: Target object to replace (if replace=True)
        replace: Replace target_obj's mesh data vs create new object

    Returns:
        bpy.types.Object: Imported or updated object

    Raises:
        RuntimeError: If import fails
    """
    try:
        # Determine format from extension
        ext = os.path.splitext(filepath)[1].lower()

        if ext == '.obj':
            bpy.ops.wm.obj_import(filepath=filepath)
        elif ext == '.ply':
            bpy.ops.wm.ply_import(filepath=filepath)
        else:
            raise ValueError(f"Unsupported format: {ext}")

        # Get imported object (last selected)
        imported_obj = bpy.context.selected_objects[-1]

        if replace and target_obj:
            # Replace target object's mesh data
            old_mesh = target_obj.data
            target_obj.data = imported_obj.data

            # Remove imported object (keep only mesh data)
            bpy.data.objects.remove(imported_obj)

            # Clean up old mesh data
            if old_mesh.users == 0:
                bpy.data.meshes.remove(old_mesh)

            return target_obj
        else:
            # Return new object
            return imported_obj

    except Exception as ex:
        raise RuntimeError(f"Failed to import mesh: {ex}")


def get_temp_dir():
    """
    Get temporary directory (ramdisk if available).

    Returns:
        str: Path to temp directory
    """
    import platform

    if platform.system() == 'Linux':
        # Use /dev/shm (tmpfs, in-memory) if available
        if os.path.exists('/dev/shm'):
            return '/dev/shm'

    # Fall back to system temp
    return tempfile.gettempdir()
