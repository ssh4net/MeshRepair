# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
# ##### END GPL LICENSE BLOCK #####

from . import main_panel
from . import subpanels


def register():
    main_panel.register()
    subpanels.register()


def unregister():
    subpanels.unregister()
    main_panel.unregister()
