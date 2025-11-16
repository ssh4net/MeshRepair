# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
# ##### END GPL LICENSE BLOCK #####

"""
Low-level IPC protocol functions

Handles binary message framing and JSON serialization.
"""

import struct
import json
import sys

# Message type constants
MSG_TYPE_COMMAND = 0x01
MSG_TYPE_RESPONSE = 0x02
MSG_TYPE_EVENT = 0x03


def read_message(stream):
    """
    Read a message from the engine output stream.

    Message format: [length:4][type:1][payload:N]

    Message types:
        0x01 = COMMAND (sent by addon to engine)
        0x02 = RESPONSE (sent by engine to addon)
        0x03 = EVENT (progress/log events sent by engine)

    Args:
        stream: Input stream (engine stdout)

    Returns:
        tuple: (msg_type, message_dict)

    Raises:
        RuntimeError: If stream closed or invalid message
    """
    # Read length (4 bytes, little-endian)
    length_bytes = force_read_bytes(stream, 4)
    length = struct.unpack('<I', length_bytes)[0]

    # Read type (1 byte)
    type_bytes = force_read_bytes(stream, 1)
    msg_type = struct.unpack('B', type_bytes)[0]

    # Read payload
    payload_bytes = force_read_bytes(stream, length)

    # Parse JSON
    payload_str = payload_bytes.decode('utf-8')
    msg = json.loads(payload_str)

    return (msg_type, msg)


def write_message(stream, msg):
    """
    Write a message to the engine input stream.

    Message format: [length:4][type:1][payload:N]

    Args:
        stream: Output stream (engine stdin)
        msg: Dictionary to send

    Raises:
        RuntimeError: If stream closed or serialization failed
    """
    # Serialize to JSON
    payload_str = json.dumps(msg)
    payload_bytes = payload_str.encode('utf-8')

    # Write length (4 bytes, little-endian)
    length = len(payload_bytes)
    stream.write(struct.pack('<I', length))

    # Write type (1 byte) - always 0x01 for commands
    stream.write(struct.pack('B', 0x01))

    # Write payload
    stream.write(payload_bytes)
    stream.flush()


def force_read_bytes(stream, count):
    """
    Read exactly count bytes from stream, blocking until available.

    Args:
        stream: Input stream
        count: Number of bytes to read

    Returns:
        bytes: Exactly count bytes

    Raises:
        RuntimeError: If stream closed before count bytes read
    """
    buf = b''
    while len(buf) < count:
        chunk = stream.read(count - len(buf))
        if not chunk:
            raise RuntimeError("Stream closed unexpectedly")
        buf += chunk
    return buf


def connection_thread_func(stream, queue):
    """
    Background thread function: continuously read messages from engine.

    Filters out EVENT messages (progress/log) and only queues RESPONSE messages.

    Args:
        stream: Engine stdout
        queue: queue.Queue for thread-safe message passing

    Note:
        This function runs in a daemon thread and exits on exception.
    """
    try:
        while True:
            msg_type, msg = read_message(stream)

            if msg_type == MSG_TYPE_RESPONSE:
                # Queue response messages for read_response()
                queue.put(msg)
            elif msg_type == MSG_TYPE_EVENT:
                # Log event messages but don't queue them
                event_type = msg.get('type', 'unknown')
                if event_type == 'progress':
                    # Progress events - log to console
                    progress = msg.get('progress', 0.0)
                    status = msg.get('status', '')
                    print(f"[MeshRepair Progress] {progress*100:.0f}% - {status}", file=sys.stdout)
                    sys.stdout.flush()
                elif event_type == 'log':
                    # Log events - log to console
                    level = msg.get('level', 'INFO')
                    message = msg.get('message', '')
                    print(f"[MeshRepair Engine {level}] {message}", file=sys.stdout)
                    sys.stdout.flush()
                # Don't queue events - they're not command responses
            else:
                # Unknown message type - log warning
                print(f"[MeshRepair WARNING] Unknown message type: {msg_type}", file=sys.stderr)
                sys.stderr.flush()

    except Exception as ex:
        # Engine died or pipe closed
        error_msg = {
            'type': 'error',
            'error': {
                'message': f"Connection error: {str(ex)}"
            }
        }
        queue.put(error_msg)
