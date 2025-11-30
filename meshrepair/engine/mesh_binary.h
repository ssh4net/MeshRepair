#pragma once

#include "../include/types.h"
#include "../include/mesh_loader.h"  // For PolygonSoup
#include <vector>
#include <string>
#include <cstdint>

namespace MeshRepair {
namespace Engine {

    /**
     * Binary mesh format for efficient IPC transfer.
     *
     * Format:
     *   [vertex_count: uint32_t]
     *   [x: float][y: float][z: float] ... (vertex_count times)
     *   [face_count: uint32_t]
     *   [i0: uint32_t][i1: uint32_t][i2: uint32_t] ... (face_count times)
     *
     * All values are little-endian.
     */

    /**
     * Serialize mesh to binary format.
     *
     * @param mesh CGAL Surface_mesh
     * @return Binary data as vector of bytes
     */
    std::vector<uint8_t> serialize_mesh_binary(const Mesh& mesh);

    /**
     * Deserialize mesh from binary format.
     *
     * @param data Binary data
     * @param expected_vertices Optional sanity check; 0 to skip check
     * @param expected_faces Optional sanity check; 0 to skip check
     * @return CGAL Surface_mesh
     * @throws std::runtime_error if data is invalid
     */
    Mesh deserialize_mesh_binary(const std::vector<uint8_t>& data, uint32_t expected_vertices = 0,
                                 uint32_t expected_faces = 0);

    /**
     * Deserialize mesh from binary format directly into polygon soup.
     *
     * @param data Binary data
     * @param expected_vertices Optional sanity check; 0 to skip check
     * @param expected_faces Optional sanity check; 0 to skip check
     * @return PolygonSoup with points + triangle indices
     * @throws std::runtime_error if data is invalid
     */
    PolygonSoup deserialize_mesh_binary_to_soup(const std::vector<uint8_t>& data, uint32_t expected_vertices = 0,
                                                uint32_t expected_faces = 0);

    /**
     * Encode binary data as base64 string.
     *
     * @param data Binary data
     * @return Base64 string
     */
    std::string base64_encode(const std::vector<uint8_t>& data);

    /**
     * Decode base64 string to binary data.
     *
     * @param encoded Base64 string
     * @return Binary data
     * @throws std::runtime_error if string is invalid
     */
    std::vector<uint8_t> base64_decode(const std::string& encoded);

}  // namespace Engine
}  // namespace MeshRepair
