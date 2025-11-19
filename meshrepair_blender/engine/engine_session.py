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
import sys
import tempfile
from .manager import EngineManager
from ..utils.mesh_binary import encode_mesh_base64, decode_mesh_base64


class EngineSession:
    """
    High-level engine session for mesh repair operations.

    Manages engine lifecycle and provides simple API for operators.

    Supports two modes:
    1. Interactive mode (socket): Bidirectional request-response
    2. Batch mode (pipe): One-way write-once pattern (UVPackmaster style)
    """

    def __init__(self, engine_path, verbosity=1, socket_mode=False, socket_host="localhost", socket_port=9876,
                 batch_mode=None, temp_dir=""):
        """
        Initialize engine session.

        Args:
            engine_path: Path to meshrepair executable (not required in socket mode)
            verbosity: Verbosity level 0-3 (0=quiet, 1=info/stats, 2=verbose, 3=debug)
            socket_mode: Use socket connection instead of subprocess
            socket_host: Hostname for socket connection
            socket_port: Port for socket connection
            batch_mode: If True, use batch mode (one-way). If None, auto-detect (True for pipe, False for socket)
        """
        self.engine_path = engine_path if engine_path else ""
        self.manager = EngineManager(self.engine_path, socket_mode, socket_host, socket_port, verbosity)
        self.mesh_loaded = False
        self.verbosity = verbosity
        self.log_file_path = None

        resolved_temp = (temp_dir or "").strip()
        if not resolved_temp and self.verbosity >= 4:
            resolved_temp = tempfile.gettempdir()
        if resolved_temp:
            resolved_temp = os.path.expanduser(resolved_temp)
            try:
                import bpy  # pylint: disable=import-outside-toplevel
                resolved_temp = bpy.path.abspath(resolved_temp)
            except Exception:
                resolved_temp = os.path.abspath(resolved_temp)
            try:
                os.makedirs(resolved_temp, exist_ok=True)
            except OSError:
                pass
        self.temp_dir = resolved_temp

        # Auto-detect batch mode: use batch for pipe mode, interactive for socket mode
        if batch_mode is None:
            self.batch_mode = not socket_mode  # Pipe = batch, Socket = interactive
        else:
            self.batch_mode = batch_mode

        # Batch mode: accumulate commands instead of sending immediately
        self.command_queue = []
        self.response_queue = []
        self.batch_started = False

    def _send_or_queue(self, cmd, timeout=60.0):
        """
        Helper: Send command immediately (interactive) or queue it (batch).

        Args:
            cmd: Command dictionary
            timeout: Timeout for interactive mode

        Returns:
            Response dict (interactive) or queued message (batch)
        """
        if self.batch_mode:
            # Batch mode: queue command
            self.command_queue.append(cmd)
            return {"type": "success", "message": f"{cmd['command']} queued for batch"}
        else:
            # Interactive mode: send and wait for response
            self.manager.send_command(cmd)
            response = self.manager.read_response(timeout=timeout)
            if response.get('type') == 'error':
                raise RuntimeError(f"{cmd['command']} failed: {response.get('error', {}).get('message', 'Unknown error')}")
            return response

    def flush_batch(self):
        """
        Execute queued commands in batch mode if any remain.

        Returns:
            list: Responses returned by the engine (empty if nothing executed)
        """
        if not self.batch_mode:
            return []

        if not self.command_queue:
            return []

        return self.execute_batch()

    def get_last_response(self, command_name):
        """
        Retrieve the most recent response for a given command from the last batch.

        Args:
            command_name (str): Command identifier to search for.

        Returns:
            dict or None: Command response if available.
        """
        if not self.response_queue:
            return None

        for entry in reversed(self.response_queue):
            if entry.get('command') == command_name:
                return entry.get('response')
        return None

    def start(self):
        """Start engine process."""
        self.manager.start()

        # Create temp log file for engine
        import tempfile
        log_fd, log_path = tempfile.mkstemp(suffix='.log', prefix='meshrepair_engine_')
        import os
        os.close(log_fd)  # Close fd, engine will open it
        self.log_file_path = log_path

        print(f"[MeshRepair INFO] Engine log file: {log_path}")

        # Initialize engine
        init_cmd = {
            "command": "init",
            "params": {
                "max_threads": 0,  # Auto-detect
                "verbose": (self.verbosity >= 2),  # Verbose mode at level 2+
                "debug": (self.verbosity >= 4),  # Debug mode (PLY dumps) at level 4 (trace)
                "log_file_path": log_path  # File logging for debugging
            }
        }
        if self.temp_dir:
            init_cmd["params"]["temp_dir"] = self.temp_dir

        if self.batch_mode:
            # Batch mode: queue command instead of sending
            self.command_queue.append(init_cmd)
            self.batch_started = True
            return {"type": "success", "message": "Init queued for batch"}
        else:
            # Interactive mode: send and wait for response
            self.manager.send_command(init_cmd)
            response = self.manager.read_response(timeout=5.0)
            if response.get('type') == 'error':
                raise RuntimeError(f"Engine init failed: {response.get('error', {}).get('message', 'Unknown error')}")

            # Store version and build info
            self.engine_version = response.get('version', 'unknown')
            self.build_date = response.get('build_date', 'unknown')
            self.build_time = response.get('build_time', 'unknown')
            print(f"[MeshRepair INFO] Engine version: {self.engine_version}, built: {self.build_date} {self.build_time}")
            sys.stdout.flush()
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
            dict: Response from engine (or queued message in batch mode)
        """
        if not self.manager.is_running():
            self.start()

        # Encode mesh as binary (base64)
        mesh_binary = encode_mesh_base64(mesh_data)

        if self.verbosity >= 2:
            print(f"[MeshRepair INFO] Sending mesh to engine: {len(mesh_data['vertices'])} vertices, {len(mesh_data['faces'])} faces")

        cmd = {
            "command": "load_mesh",
            "params": {
                "mesh_data_binary": mesh_binary
            }
        }

        if self.batch_mode:
            # Batch mode: queue command
            self.command_queue.append(cmd)
            self.mesh_loaded = True
            return {"type": "success", "message": "Load mesh queued for batch"}
        else:
            # Interactive mode: send and wait for response
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
            dict: Response with preprocessing stats (or queued message in batch mode)
        """
        if not self.mesh_loaded:
            raise RuntimeError("No mesh loaded")

        cmd = {
            "command": "preprocess",
            "params": options
        }

        response = self._send_or_queue(cmd, timeout=120.0)

        # Print timing stats if available (interactive mode only)
        if not self.batch_mode:
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
            dict: Response with hole statistics (or queued message in batch mode)
        """
        if not self.mesh_loaded:
            raise RuntimeError("No mesh loaded")

        cmd = {
            "command": "detect_holes",
            "params": options
        }

        response = self._send_or_queue(cmd, timeout=120.0)

        # Print timing stats if available (interactive mode only)
        if not self.batch_mode:
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
            dict: Response with filling statistics (or queued message in batch mode)
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

        response = self._send_or_queue(cmd, timeout=600.0)

        # Print timing stats if available (interactive mode only)
        if not self.batch_mode:
            data = response.get('data', {})
            if 'fill_time_ms' in data:
                print(f"[MeshRepair STATS] Hole filling time: {data['fill_time_ms']} ms")

        return response

    def save_mesh(self):
        """
        Get repaired mesh data from engine (using binary format).

        In batch mode, this queues the save_mesh command, executes the batch,
        and extracts the mesh from the response.

        Returns:
            dict: Mesh data with 'vertices' and 'faces' keys
        """
        if not self.mesh_loaded:
            raise RuntimeError("No mesh loaded")

        if self.verbosity >= 2:
            print("[MeshRepair INFO] Requesting mesh from engine...")

        cmd = {
            "command": "save_mesh",
            "params": {
                "return_binary": True  # Return binary data via pipe
            }
        }

        if self.batch_mode:
            # Batch mode: queue command, execute batch, extract mesh from recorded response
            self.command_queue.append(cmd)

            # Execute the entire batch
            responses = self.execute_batch()

            # Prefer mapped response (includes command name)
            save_response = self.get_last_response("save_mesh")

            # Fallback: search raw responses for mesh payload
            if not save_response:
                for response_entry in reversed(responses):
                    if response_entry.get('type') != 'error' and 'mesh_data_binary' in response_entry:
                        save_response = response_entry
                        break

            if not save_response:
                raise RuntimeError("No save_mesh response found in batch")

            response = save_response
        else:
            # Interactive mode: send and wait
            self.manager.send_command(cmd)
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

        if self.verbosity >= 2:
            print(f"[MeshRepair INFO] Decoding mesh from engine...")

        # Decode binary mesh
        mesh_data = decode_mesh_base64(mesh_binary)

        if self.verbosity >= 2:
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

    def execute_batch(self):
        """
        Execute all queued commands in batch mode.

        This sends all queued commands to the engine at once, then reads
        responses. Only works in batch mode.

        Returns:
            list: List of responses in order
        """
        if not self.batch_mode:
            raise RuntimeError("execute_batch() only works in batch mode")

        if not self.command_queue:
            raise RuntimeError("No commands queued for batch execution")

        print(f"[MeshRepair INFO] Executing batch: {len(self.command_queue)} commands")

        # Send all commands at once and close stdin
        self.manager.send_commands_batch(self.command_queue)

        # Read responses (one for each command)
        responses = []
        paired_responses = []
        for i, cmd in enumerate(self.command_queue):
            cmd_name = cmd.get('command', 'unknown')
            print(f"[MeshRepair INFO] Waiting for response {i+1}/{len(self.command_queue)}: {cmd_name}")

            response = self.manager.read_response(timeout=120.0)
            responses.append(response)
            paired_responses.append({
                "command": cmd_name,
                "response": response
            })

            # Check for errors
            if response.get('type') == 'error':
                error_msg = response.get('error', {}).get('message', 'Unknown error')
                print(f"[MeshRepair ERROR] Command '{cmd_name}' failed: {error_msg}")
            else:
                print(f"[MeshRepair INFO] Command '{cmd_name}' succeeded")

            # If this is the init response, extract and print build info
            if cmd_name == 'init' and response.get('type') == 'success':
                version = response.get('version', 'unknown')
                build_date = response.get('build_date', 'unknown')
                build_time = response.get('build_time', 'unknown')
                print(f"[MeshRepair INFO] Engine version: {version}, built: {build_date} {build_time}")
                sys.stdout.flush()

        # Clear command queue
        self.command_queue = []
        self.response_queue = paired_responses

        # In batch mode, terminate the engine process now that we have all responses
        # The engine is waiting indefinitely for termination (keeps stdout pipe open)
        # We must kill it to avoid orphan processes
        print(f"[MeshRepair INFO] All responses received, terminating engine...")
        if self.manager.is_running():
            try:
                self.manager.engine_proc.terminate()
                # Give it a moment to terminate gracefully
                self.manager.engine_proc.wait(timeout=1.0)
            except:
                # Force kill if terminate didn't work
                self.manager.engine_proc.kill()
                self.manager.engine_proc.wait()
            print(f"[MeshRepair INFO] Engine terminated")

        return responses

    def queue_command(self, command_dict):
        """
        Queue a command for batch execution.

        Args:
            command_dict: Command to queue

        Only works in batch mode.
        """
        if not self.batch_mode:
            raise RuntimeError("queue_command() only works in batch mode")

        self.command_queue.append(command_dict)

    def stop(self):
        """Stop engine process and clean up."""
        try:
            self.manager.stop()
        finally:
            self.mesh_loaded = False
            # Print log file location if available
            if self.log_file_path:
                print(f"[MeshRepair INFO] Engine log saved to: {self.log_file_path}")

    def __enter__(self):
        """Context manager entry."""
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.stop()
