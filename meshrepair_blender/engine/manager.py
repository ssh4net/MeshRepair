# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
# ##### END GPL LICENSE BLOCK #####

"""
Engine process manager

Handles engine lifecycle: spawning, communication, and cleanup.
Supports both subprocess (pipe) mode and socket mode.
"""

import subprocess
import threading
import queue
import sys
import socket
from .connection import write_message, connection_thread_func


def stderr_thread_func(stream):
    """
    Background thread function: read stderr from engine and log it.

    Args:
        stream: Engine stderr
    """
    try:
        for line in iter(stream.readline, b''):
            if line:
                msg = line.decode('utf-8', errors='replace').rstrip()
                print(f"[MeshRepair Engine STDERR] {msg}", file=sys.stdout)
                sys.stdout.flush()
    except Exception:
        pass


class EngineManager:
    """
    Manages the meshrepair_engine connection (subprocess or socket).

    Responsibilities:
    - Spawn engine process (pipe mode) or connect to socket (socket mode)
    - Send commands via stdin/socket
    - Receive responses via stdout/socket (background thread)
    - Clean shutdown
    """

    def __init__(self, engine_path, socket_mode=False, socket_host="localhost", socket_port=9876, verbose=False, show_stats=False):
        """
        Initialize engine manager.

        Args:
            engine_path: Path to meshrepair_engine executable
            socket_mode: If True, connect via TCP socket instead of subprocess
            socket_host: Hostname for socket connection
            socket_port: Port for socket connection
            verbose: Enable verbose output from engine (--verbose flag)
            show_stats: Enable detailed statistics output from engine (--stats flag)
        """
        self.engine_path = engine_path
        self.socket_mode = socket_mode
        self.socket_host = socket_host
        self.socket_port = socket_port
        self.verbose = verbose
        self.show_stats = show_stats

        # Subprocess mode
        self.engine_proc = None
        self.stderr_thread = None

        # Socket mode
        self.socket_conn = None
        self.socket_file = None  # File-like object for socket

        # Common
        self.connection_thread = None
        self.message_queue = None

    def start(self):
        """
        Start the engine connection (subprocess or socket).

        Returns:
            subprocess.Popen or socket.socket: Connection handle

        Raises:
            RuntimeError: If connection fails
        """
        if self.socket_mode:
            return self._start_socket_mode()
        else:
            return self._start_subprocess_mode()

    def _start_subprocess_mode(self):
        """Start engine as subprocess (pipe mode)."""
        try:
            # Build command line arguments
            args = [self.engine_path, '--engine']
            if self.verbose:
                args.append('--verbose')
            if self.show_stats:
                args.append('--stats')

            # Log engine startup
            print(f"[MeshRepair INFO] Starting engine: {' '.join(args)}", file=sys.stdout)
            sys.stdout.flush()

            # Spawn engine process with --engine flag to enable IPC mode
            self.engine_proc = subprocess.Popen(
                args,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                bufsize=0  # Unbuffered
            )

            # Create message queue
            self.message_queue = queue.Queue()

            # Start background thread for reading stdout
            self.connection_thread = threading.Thread(
                target=connection_thread_func,
                args=(self.engine_proc.stdout, self.message_queue),
                daemon=True
            )
            self.connection_thread.start()

            # Start background thread for reading stderr
            self.stderr_thread = threading.Thread(
                target=stderr_thread_func,
                args=(self.engine_proc.stderr,),
                daemon=True
            )
            self.stderr_thread.start()

            print(f"[MeshRepair INFO] Engine started with PID {self.engine_proc.pid}", file=sys.stdout)
            sys.stdout.flush()

            return self.engine_proc

        except Exception as ex:
            raise RuntimeError(f"Failed to start engine: {ex}")

    def _start_socket_mode(self):
        """Connect to engine via TCP socket."""
        try:
            print(f"[MeshRepair INFO] Connecting to engine at {self.socket_host}:{self.socket_port}", file=sys.stdout)
            sys.stdout.flush()

            # Create socket connection
            self.socket_conn = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket_conn.connect((self.socket_host, self.socket_port))

            # Create file-like object for socket
            self.socket_file = self.socket_conn.makefile('rwb', buffering=0)

            # Create message queue
            self.message_queue = queue.Queue()

            # Start background thread for reading from socket
            self.connection_thread = threading.Thread(
                target=connection_thread_func,
                args=(self.socket_file, self.message_queue),
                daemon=True
            )
            self.connection_thread.start()

            print(f"[MeshRepair INFO] Connected to engine via socket", file=sys.stdout)
            sys.stdout.flush()

            return self.socket_conn

        except Exception as ex:
            raise RuntimeError(f"Failed to connect to engine: {ex}")

    def send_command(self, command_dict):
        """
        Send a command to the engine.

        Args:
            command_dict: Command dictionary (will be JSON-encoded)

        Raises:
            RuntimeError: If engine not running
        """
        if not self.is_running():
            raise RuntimeError("Engine not running")

        # Log command (but not mesh data - too large)
        cmd_name = command_dict.get('command', 'unknown')

        # Check if mesh_data is in params (don't log it!)
        params = command_dict.get('params', {})
        if 'mesh_data' in params:
            verts = len(params['mesh_data'].get('vertices', []))
            faces = len(params['mesh_data'].get('faces', []))
            print(f"[MeshRepair INFO] Sending command: {cmd_name} (mesh: {verts} verts, {faces} faces)",
                  file=sys.stdout)
        elif 'mesh_data_binary' in params:
            binary_size = len(params['mesh_data_binary'])
            print(f"[MeshRepair INFO] Sending command: {cmd_name} (binary: {binary_size} bytes)",
                  file=sys.stdout)
        else:
            # Log command with params (excluding mesh_data)
            print(f"[MeshRepair INFO] Sending command: {cmd_name}", file=sys.stdout)
        sys.stdout.flush()

        # Write to appropriate stream
        if self.socket_mode:
            write_message(self.socket_file, command_dict)
        else:
            write_message(self.engine_proc.stdin, command_dict)

    def read_response(self, timeout=None):
        """
        Read a response from the engine (blocking).

        Args:
            timeout: Timeout in seconds (None = wait forever)

        Returns:
            dict: Response message

        Raises:
            queue.Empty: If timeout expires
        """
        if not self.message_queue:
            raise RuntimeError("Engine not started")

        response = self.message_queue.get(timeout=timeout)

        # Log response (but not mesh data - too large)
        resp_type = response.get('type', 'unknown')
        if resp_type == 'error':
            error_msg = response.get('error', {}).get('message', 'Unknown error')
            print(f"[MeshRepair ERROR] Engine response: {resp_type} - {error_msg}", file=sys.stdout)
        else:
            # Check if response has mesh data (JSON or binary)
            if 'mesh_data_binary' in response:
                binary_size = len(response['mesh_data_binary'])
                print(f"[MeshRepair INFO] Engine response: {resp_type} (binary: {binary_size} bytes)",
                      file=sys.stdout)
            elif 'mesh_data' in response:
                mesh_data = response['mesh_data']
                verts = len(mesh_data.get('vertices', []))
                faces = len(mesh_data.get('faces', []))
                print(f"[MeshRepair INFO] Engine response: {resp_type} (mesh: {verts} verts, {faces} faces)",
                      file=sys.stdout)
            else:
                # Don't log the entire response - just the message
                msg = response.get('message', '')
                print(f"[MeshRepair INFO] Engine response: {resp_type} - {msg}", file=sys.stdout)
        sys.stdout.flush()

        return response

    def try_read_response(self):
        """
        Try to read a response without blocking.

        Returns:
            dict or None: Response message if available, None otherwise
        """
        if not self.message_queue:
            return None

        try:
            return self.message_queue.get_nowait()
        except queue.Empty:
            return None

    def is_running(self):
        """
        Check if engine connection is still active.

        Returns:
            bool: True if connection is active
        """
        if self.socket_mode:
            return self.socket_conn is not None
        else:
            if not self.engine_proc:
                return False
            return self.engine_proc.poll() is None

    def stop(self, timeout=5.0):
        """
        Stop the engine connection gracefully.

        Args:
            timeout: Seconds to wait before force close

        Returns:
            int: Engine exit code (subprocess mode) or 0 (socket mode)
        """
        if self.socket_mode:
            return self._stop_socket_mode()
        else:
            return self._stop_subprocess_mode(timeout)

    def _stop_subprocess_mode(self, timeout=5.0):
        """Stop engine subprocess."""
        if not self.engine_proc:
            return 0

        # Try graceful shutdown
        try:
            self.send_command({"command": "shutdown"})
            self.engine_proc.wait(timeout=timeout)
        except (RuntimeError, subprocess.TimeoutExpired):
            # Force kill
            self.engine_proc.kill()
            self.engine_proc.wait()

        exit_code = self.engine_proc.returncode
        self.engine_proc = None
        return exit_code

    def _stop_socket_mode(self):
        """Close socket connection (do NOT shutdown engine)."""
        if not self.socket_conn:
            return 0

        # In socket mode, do NOT send shutdown command
        # The engine is manually managed by the user
        print("[MeshRepair INFO] Disconnecting from engine (engine will continue running)", file=sys.stdout)
        sys.stdout.flush()

        # Close socket connection
        try:
            if self.socket_file:
                self.socket_file.close()
            if self.socket_conn:
                self.socket_conn.close()
        except OSError:
            pass

        self.socket_file = None
        self.socket_conn = None
        return 0
