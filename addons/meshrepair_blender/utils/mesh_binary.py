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

    # Calculate buffer size
    # 4 bytes (vertex count) + 12 bytes per vertex + 4 bytes (face count) + 12 bytes per face
    buffer_size = 4 + len(vertices) * 12 + 4 + len(faces) * 12

    # Create binary buffer
    buffer = bytearray(buffer_size)
    offset = 0

    # Write vertex count (uint32, little-endian)
    struct.pack_into('<I', buffer, offset, len(vertices))
    offset += 4

    # Write vertices (float, float, float for each)
    for vertex in vertices:
        struct.pack_into('<fff', buffer, offset, vertex[0], vertex[1], vertex[2])
        offset += 12

    # Write face count (uint32, little-endian)
    struct.pack_into('<I', buffer, offset, len(faces))
    offset += 4

    # Write faces (uint32, uint32, uint32 for each)
    for face in faces:
        struct.pack_into('<III', buffer, offset, face[0], face[1], face[2])
        offset += 12

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
    if len(binary_data) < 8:
        raise ValueError("Binary mesh data too small")

    offset = 0

    # Read vertex count
    vertex_count = struct.unpack_from('<I', binary_data, offset)[0]
    offset += 4

    # Validate size
    expected_size = 4 + vertex_count * 12 + 4
    if len(binary_data) < expected_size:
        raise ValueError("Binary mesh data truncated (vertices)")

    # Read vertices
    vertices = []
    for i in range(vertex_count):
        x, y, z = struct.unpack_from('<fff', binary_data, offset)
        vertices.append([x, y, z])
        offset += 12

    # Read face count
    if len(binary_data) < offset + 4:
        raise ValueError("Binary mesh data truncated (face count)")

    face_count = struct.unpack_from('<I', binary_data, offset)[0]
    offset += 4

    # Validate size
    if len(binary_data) < offset + face_count * 12:
        raise ValueError("Binary mesh data truncated (faces)")

    # Read faces
    faces = []
    for i in range(face_count):
        i0, i1, i2 = struct.unpack_from('<III', binary_data, offset)

        # Validate indices
        if i0 >= vertex_count or i1 >= vertex_count or i2 >= vertex_count:
            raise ValueError(f"Face index out of range: [{i0}, {i1}, {i2}]")

        faces.append([i0, i1, i2])
        offset += 12

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
