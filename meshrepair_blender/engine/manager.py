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

    def __init__(self, engine_path, socket_mode=False, socket_host="localhost", socket_port=9876, verbosity=1):
        """
        Initialize engine manager.

        Args:
            engine_path: Path to meshrepair_engine executable
            socket_mode: If True, connect via TCP socket instead of subprocess
            socket_host: Hostname for socket connection
            socket_port: Port for socket connection
            verbosity: Verbosity level 0-3 (0=quiet, 1=info/stats, 2=verbose, 3=debug)
        """
        self.engine_path = engine_path
        self.socket_mode = socket_mode
        self.socket_host = socket_host
        self.socket_port = socket_port
        self.verbosity = verbosity

        # Subprocess mode
        self.engine_proc = None
        self.stderr_thread = None

        # Socket mode
        self.socket_conn = None
        self.socket_file = None  # File-like object for socket

        # Common
        self.connection_thread = None
        self.message_queue = None
        self.batch_active = False  # Track if batch mode is active (stdin closed)

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
            if self.verbosity > 0:
                args.extend(['-v', str(self.verbosity)])

            # Log engine startup
            print(f"[MeshRepair INFO] Starting engine: {' '.join(args)}", file=sys.stdout)
            sys.stdout.flush()

            # Windows-specific: CREATE_NEW_PROCESS_GROUP prevents pipe closure issues
            popen_kwargs = {
                'stdin': subprocess.PIPE,
                'stdout': subprocess.PIPE,
                # NOTE: We do NOT pipe stderr on Windows - let it go to console
                # Piping stderr can cause file descriptor mix-ups and binary corruption
                # stderr goes to parent's stderr (Blender console) directly
                # Use default buffering (like UVPackmaster) - no bufsize parameter
            }

            if sys.platform == 'win32':
                # CREATE_NEW_PROCESS_GROUP for better process isolation
                popen_kwargs['creationflags'] = subprocess.CREATE_NEW_PROCESS_GROUP
            else:
                # On Unix-like systems, it's safe to pipe stderr
                popen_kwargs['stderr'] = subprocess.PIPE

            # Spawn engine process with --engine flag to enable IPC mode
            self.engine_proc = subprocess.Popen(args, **popen_kwargs)

            # Create message queue
            self.message_queue = queue.Queue()

            # Use stdout stream directly (default buffering works on all platforms)
            stdout_stream = self.engine_proc.stdout
            print(f"[MeshRepair DEBUG] Stdout stream type: {type(stdout_stream)}", file=sys.stdout)
            sys.stdout.flush()

            # Start background thread for reading stdout
            self.connection_thread = threading.Thread(
                target=connection_thread_func,
                args=(stdout_stream, self.message_queue),
                daemon=True
            )
            self.connection_thread.start()

            # Start background thread for reading stderr (only if piped)
            if self.engine_proc.stderr is not None:
                self.stderr_thread = threading.Thread(
                    target=stderr_thread_func,
                    args=(self.engine_proc.stderr,),
                    daemon=True
                )
                self.stderr_thread.start()
            else:
                # stderr goes directly to console - no thread needed
                print(f"[MeshRepair INFO] Engine stderr output will appear directly in console", file=sys.stdout)

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

    def send_commands_batch(self, commands):
        """
        Send multiple commands at once (batch mode).

        This sends all commands to stdin, then closes stdin to signal end of input.
        The engine will process all commands and send responses via stdout.
        This pattern avoids Windows pipe EOF issues with bidirectional communication.

        Args:
            commands: List of command dictionaries

        Raises:
            RuntimeError: If engine not running
        """
        if not self.is_running():
            raise RuntimeError("Engine not running")

        print(f"[MeshRepair INFO] Sending batch of {len(commands)} commands", file=sys.stdout)
        sys.stdout.flush()

        # Write all commands to stdin
        for cmd in commands:
            if self.socket_mode:
                write_message(self.socket_file, cmd)
            else:
                write_message(self.engine_proc.stdin, cmd)

        # Close stdin to signal end of commands (pipe mode only)
        # This is the key to avoiding Windows pipe EOF issues
        if not self.socket_mode and self.engine_proc.stdin:
            print(f"[MeshRepair INFO] Closing stdin (batch mode)", file=sys.stdout)
            sys.stdout.flush()
            self.engine_proc.stdin.close()
            self.batch_active = True  # Mark batch as active

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

        # In batch mode, stdin is already closed, so we can't send shutdown command
        # Just terminate the process (it's already waiting for termination)
        if self.batch_active:
            try:
                # Process might already be terminated by execute_batch()
                if self.engine_proc.poll() is None:
                    self.engine_proc.terminate()
                    self.engine_proc.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                self.engine_proc.kill()
                self.engine_proc.wait()
        else:
            # Interactive mode: try graceful shutdown
            try:
                self.send_command({"command": "shutdown"})
                self.engine_proc.wait(timeout=timeout)
            except (RuntimeError, subprocess.TimeoutExpired):
                # Force kill
                self.engine_proc.kill()
                self.engine_proc.wait()

        exit_code = self.engine_proc.returncode
        self.engine_proc = None
        self.batch_active = False
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
