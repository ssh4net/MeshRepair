# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
# ##### END GPL LICENSE BLOCK #####

"""
Binary mesh serialization for efficient IPC transfer.

Format (all values little-endian):
  [vertex_count: uint32]
  [x: float][y: float][z: float] ... (vertex_count times)
  [face_count: uint32]
  [i0: uint32][i1: uint32][i2: uint32] ... (face_count times)
"""

import struct
import base64
import array
import sys


def serialize_mesh_binary(mesh_data):
    """
    Serialize mesh data to binary format.

    Args:
        mesh_data: Dict with 'vertices' and 'faces' keys
                  vertices: [[x, y, z], ...]
                  faces: [[i0, i1, i2], ...]

    Returns:
        bytes: Binary mesh data
    """
    vertices = mesh_data['vertices']
    faces = mesh_data['faces']

    vertex_count = len(vertices)
    face_count = len(faces)

    # Flatten using array for contiguous memory and less Python overhead
    coords = array.array('f', (coord for v in vertices for coord in v))
    indices = array.array('I', (idx for tri in faces for idx in tri))

    if sys.byteorder != 'little':
        coords.byteswap()
        indices.byteswap()

    buffer = bytearray()
    buffer.extend(struct.pack('<I', vertex_count))
    buffer.extend(coords.tobytes())
    buffer.extend(struct.pack('<I', face_count))
    buffer.extend(indices.tobytes())
    return bytes(buffer)


def deserialize_mesh_binary(binary_data):
    """
    Deserialize mesh from binary format.

    Args:
        binary_data: Binary mesh data (bytes)

    Returns:
        dict: Mesh data with 'vertices' and 'faces' keys

    Raises:
        ValueError: If binary data is invalid
    """
    mv = memoryview(binary_data)
    if len(mv) < 8:
        raise ValueError("Binary mesh data too small")

    offset = 0
    vertex_count = struct.unpack_from('<I', mv, offset)[0]
    offset += 4

    coord_count = vertex_count * 3
    coord_bytes = coord_count * 4
    if len(mv) < offset + coord_bytes + 4:
        raise ValueError("Binary mesh data truncated (vertices)")

    coords = array.array('f')
    coords.frombytes(mv[offset:offset + coord_bytes])
    if sys.byteorder != 'little':
        coords.byteswap()
    offset += coord_bytes

    face_count = struct.unpack_from('<I', mv, offset)[0]
    offset += 4

    index_count = face_count * 3
    index_bytes = index_count * 4
    if len(mv) < offset + index_bytes:
        raise ValueError("Binary mesh data truncated (faces)")

    indices = array.array('I')
    indices.frombytes(mv[offset:offset + index_bytes])
    if sys.byteorder != 'little':
        indices.byteswap()

    vertices = [
        [coords[i], coords[i + 1], coords[i + 2]]
        for i in range(0, coord_count, 3)
    ]

    faces = []
    for i in range(0, index_count, 3):
        i0, i1, i2 = indices[i], indices[i + 1], indices[i + 2]
        if i0 >= vertex_count or i1 >= vertex_count or i2 >= vertex_count:
            raise ValueError(f"Face index out of range: [{i0}, {i1}, {i2}]")
        faces.append([i0, i1, i2])

    return {'vertices': vertices, 'faces': faces}


def encode_mesh_base64(mesh_data):
    """
    Serialize mesh to binary and encode as base64 string.

    Args:
        mesh_data: Dict with 'vertices' and 'faces' keys

    Returns:
        str: Base64-encoded binary mesh
    """
    binary_data = serialize_mesh_binary(mesh_data)
    return base64.b64encode(binary_data).decode('ascii')


def decode_mesh_base64(base64_string):
    """
    Decode base64 string to binary and deserialize mesh.

    Args:
        base64_string: Base64-encoded binary mesh

    Returns:
        dict: Mesh data with 'vertices' and 'faces' keys
    """
    binary_data = base64.b64decode(base64_string)
    return deserialize_mesh_binary(binary_data)
