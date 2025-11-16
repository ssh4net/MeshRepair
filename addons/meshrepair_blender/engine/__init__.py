# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
# ##### END GPL LICENSE BLOCK #####

"""
Engine communication module

This module handles communication with the meshrepair_engine executable via IPC.
"""

from .connection import read_message, write_message
from .manager import EngineManager
from .engine_session import EngineSession

__all__ = ['read_message', 'write_message', 'EngineManager', 'EngineSession']
