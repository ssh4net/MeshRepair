# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software Foundation,
#  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ##### END GPL LICENSE BLOCK #####

bl_info = {
    "name": "Mesh Repair",
    "author": "MeshRepair Team",
    "version": (1, 0, 0),
    "blender": (3, 3, 0),
    "location": "View3D > Sidebar > Mesh Repair",
    "description": "Professional mesh hole filling and repair using CGAL algorithms",
    "warning": "",
    "doc_url": "https://github.com/meshrepair/meshrepair",
    "tracker_url": "https://github.com/meshrepair/meshrepair/issues",
    "category": "Mesh"
}

import bpy
from . import preferences
from . import properties
from . import operators
from . import ui


# Registration
def register():
    """Register addon classes and properties"""
    print("Mesh Repair: Registering...")

    # Register preferences
    preferences.register()

    # Register properties
    properties.register()

    # Register operators
    operators.register()

    # Register UI
    ui.register()

    # Attach properties to Scene
    bpy.types.Scene.meshrepair_props = bpy.props.PointerProperty(
        type=properties.MeshRepairSceneProperties
    )

    print("Mesh Repair: Registration complete!")


def unregister():
    """Unregister addon classes and properties"""
    print("Mesh Repair: Unregistering...")

    # Remove scene properties
    del bpy.types.Scene.meshrepair_props

    # Unregister in reverse order
    ui.unregister()
    operators.unregister()
    properties.unregister()
    preferences.unregister()

    print("Mesh Repair: Unregistration complete!")


if __name__ == "__main__":
    register()
