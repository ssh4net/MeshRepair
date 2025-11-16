# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
# ##### END GPL LICENSE BLOCK #####

"""
Engine session manager

High-level API for mesh repair operations using the engine subprocess.
"""

import os
import tempfile
from .manager import EngineManager
from ..utils.mesh_binary import encode_mesh_base64, decode_mesh_base64


class EngineSession:
    """
    High-level engine session for mesh repair operations.

    Manages engine lifecycle and provides simple API for operators.
    """

    def __init__(self, engine_path, verbose=False, show_stats=False, socket_mode=False, socket_host="localhost", socket_port=9876):
        """
        Initialize engine session.

        Args:
            engine_path: Path to meshrepair executable (not required in socket mode)
            verbose: Enable debug/verbose output from engine
            show_stats: Enable detailed statistics and timing output from engine
            socket_mode: Use socket connection instead of subprocess
            socket_host: Hostname for socket connection
            socket_port: Port for socket connection
        """
        self.engine_path = engine_path if engine_path else ""
        self.manager = EngineManager(self.engine_path, socket_mode, socket_host, socket_port, verbose, show_stats)
        self.mesh_loaded = False
        self.verbose = verbose

    def start(self):
        """Start engine process."""
        self.manager.start()

        # Initialize engine
        init_cmd = {
            "command": "init",
            "params": {
                "max_threads": 0,  # Auto-detect
                "verbose": self.verbose,  # Pass verbose flag
                "debug": self.verbose  # Enable debug mode (PLY dumps) when verbose
            }
        }
        self.manager.send_command(init_cmd)

        # Wait for initialization response
        response = self.manager.read_response(timeout=5.0)
        if response.get('type') == 'error':
            raise RuntimeError(f"Engine init failed: {response.get('error', {}).get('message', 'Unknown error')}")

        return response

    def test(self):
        """
        Test engine connection.

        Returns:
            dict: Engine info and version
        """
        if not self.manager.is_running():
            self.start()

        # Send get_info command
        cmd = {"command": "get_info"}
        self.manager.send_command(cmd)

        # Wait for response
        response = self.manager.read_response(timeout=5.0)
        if response.get('type') == 'error':
            raise RuntimeError(f"Test failed: {response.get('error', {}).get('message', 'Unknown error')}")

        return response.get('data', {})

    def load_mesh(self, mesh_data):
        """
        Load mesh into engine (using binary format).

        Args:
            mesh_data: Dict with 'vertices' and 'faces' keys (mesh soup format)

        Returns:
            dict: Response from engine
        """
        if not self.manager.is_running():
            self.start()

        # Encode mesh as binary (base64)
        mesh_binary = encode_mesh_base64(mesh_data)

        if self.verbose:
            print(f"[MeshRepair INFO] Sending mesh to engine: {len(mesh_data['vertices'])} vertices, {len(mesh_data['faces'])} faces")

        cmd = {
            "command": "load_mesh",
            "params": {
                "mesh_data_binary": mesh_binary
            }
        }
        self.manager.send_command(cmd)

        # Wait for response (may take time for large meshes)
        response = self.manager.read_response(timeout=60.0)
        if response.get('type') == 'error':
            raise RuntimeError(f"Load mesh failed: {response.get('error', {}).get('message', 'Unknown error')}")

        # Print timing stats if available
        data = response.get('data', {})
        if 'load_time_ms' in data:
            print(f"[MeshRepair STATS] Mesh load time: {data['load_time_ms']} ms")
            if 'decode_time_ms' in data:
                print(f"[MeshRepair STATS]   Base64 decode: {data['decode_time_ms']} ms")
            if 'deserialize_time_ms' in data:
                print(f"[MeshRepair STATS]   Deserialization: {data['deserialize_time_ms']} ms")

        self.mesh_loaded = True
        return response

    def preprocess(self, options):
        """
        Preprocess mesh.

        Args:
            options: Preprocessing options dict

        Returns:
            dict: Response with preprocessing stats
        """
        if not self.mesh_loaded:
            raise RuntimeError("No mesh loaded")

        cmd = {
            "command": "preprocess",
            "params": options
        }
        self.manager.send_command(cmd)

        # Wait for response (may take time for large meshes)
        response = self.manager.read_response(timeout=120.0)
        if response.get('type') == 'error':
            raise RuntimeError(f"Preprocess failed: {response.get('error', {}).get('message', 'Unknown error')}")

        # Print timing stats if available
        data = response.get('data', {})
        if 'preprocess_time_ms' in data:
            print(f"[MeshRepair STATS] Preprocessing time: {data['preprocess_time_ms']} ms")

        return response

    def detect_holes(self, options):
        """
        Detect holes in mesh.

        Args:
            options: Detection options dict

        Returns:
            dict: Response with hole statistics
        """
        if not self.mesh_loaded:
            raise RuntimeError("No mesh loaded")

        cmd = {
            "command": "detect_holes",
            "params": options
        }
        self.manager.send_command(cmd)

        # Wait for response
        response = self.manager.read_response(timeout=120.0)
        if response.get('type') == 'error':
            raise RuntimeError(f"Detect holes failed: {response.get('error', {}).get('message', 'Unknown error')}")

        # Print timing stats if available
        data = response.get('data', {})
        if 'detect_time_ms' in data:
            print(f"[MeshRepair STATS] Hole detection time: {data['detect_time_ms']} ms")

        return response

    def fill_holes(self, options):
        """
        Fill detected holes.

        Args:
            options: Filling options dict

        Returns:
            dict: Response with filling statistics
        """
        if not self.mesh_loaded:
            raise RuntimeError("No mesh loaded")

        # Merge use_partitioned into options
        params = dict(options)
        params["use_partitioned"] = options.get("use_partitioned", True)

        cmd = {
            "command": "fill_holes",
            "params": params
        }
        self.manager.send_command(cmd)

        # Wait for response (may take long time for complex meshes)
        response = self.manager.read_response(timeout=600.0)
        if response.get('type') == 'error':
            raise RuntimeError(f"Fill holes failed: {response.get('error', {}).get('message', 'Unknown error')}")

        # Print timing stats if available
        data = response.get('data', {})
        if 'fill_time_ms' in data:
            print(f"[MeshRepair STATS] Hole filling time: {data['fill_time_ms']} ms")

        return response

    def save_mesh(self):
        """
        Get repaired mesh data from engine (using binary format).

        Returns:
            dict: Mesh data with 'vertices' and 'faces' keys
        """
        if not self.mesh_loaded:
            raise RuntimeError("No mesh loaded")

        if self.verbose:
            print("[MeshRepair INFO] Requesting mesh from engine...")

        cmd = {
            "command": "save_mesh",
            "params": {
                "return_binary": True  # Return binary data via pipe
            }
        }
        self.manager.send_command(cmd)

        # Wait for response (may take time for large meshes)
        response = self.manager.read_response(timeout=120.0)
        if response.get('type') == 'error':
            raise RuntimeError(f"Save mesh failed: {response.get('error', {}).get('message', 'Unknown error')}")

        # Print timing stats if available
        data = response.get('data', {})
        if 'save_time_ms' in data:
            print(f"[MeshRepair STATS] Mesh save time: {data['save_time_ms']} ms")
            if 'serialize_time_ms' in data:
                print(f"[MeshRepair STATS]   Serialization: {data['serialize_time_ms']} ms")
            if 'encode_time_ms' in data:
                print(f"[MeshRepair STATS]   Base64 encode: {data['encode_time_ms']} ms")

        # Binary mesh data is at root level of response
        mesh_binary = response.get('mesh_data_binary', '')
        if not mesh_binary:
            raise RuntimeError("No mesh data in response")

        if self.verbose:
            print(f"[MeshRepair INFO] Decoding mesh from engine...")

        # Decode binary mesh
        mesh_data = decode_mesh_base64(mesh_binary)

        if self.verbose:
            print(f"[MeshRepair INFO] Received mesh: {len(mesh_data['vertices'])} vertices, {len(mesh_data['faces'])} faces")

        return mesh_data

    def get_mesh_info(self):
        """
        Get current mesh info.

        Returns:
            dict: Mesh statistics
        """
        if not self.mesh_loaded:
            raise RuntimeError("No mesh loaded")

        cmd = {"command": "get_info"}
        self.manager.send_command(cmd)

        response = self.manager.read_response(timeout=5.0)
        if response.get('type') == 'error':
            raise RuntimeError(f"Get info failed: {response.get('error', {}).get('message', 'Unknown error')}")

        return response.get('data', {})

    def stop(self):
        """Stop engine process and clean up."""
        try:
            self.manager.stop()
        finally:
            self.mesh_loaded = False

    def __enter__(self):
        """Context manager entry."""
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.stop()
