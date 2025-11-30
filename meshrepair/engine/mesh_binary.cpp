#include "mesh_binary.h"
#include <cstring>
#include <stdexcept>
#include <map>

namespace MeshRepair {
namespace Engine {

    // Base64 encoding table
    static const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                       "abcdefghijklmnopqrstuvwxyz"
                                       "0123456789+/";

    // Helper: Write uint32_t as little-endian
    static void write_uint32_le(std::vector<uint8_t>& buffer, uint32_t value)
    {
        buffer.push_back(static_cast<uint8_t>(value & 0xFF));
        buffer.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        buffer.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        buffer.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }

    // Helper: Read uint32_t as little-endian
    static uint32_t read_uint32_le(const uint8_t* data)
    {
        return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8)
               | (static_cast<uint32_t>(data[2]) << 16) | (static_cast<uint32_t>(data[3]) << 24);
    }

    // Helper: Write float as little-endian bytes
    static void write_float_le(std::vector<uint8_t>& buffer, float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(float));
        write_uint32_le(buffer, bits);
    }

    // Helper: Read float as little-endian bytes
    static float read_float_le(const uint8_t* data)
    {
        uint32_t bits = read_uint32_le(data);
        float value;
        std::memcpy(&value, &bits, sizeof(float));
        return value;
    }

    std::vector<uint8_t> serialize_mesh_binary(const Mesh& mesh)
    {
        std::vector<uint8_t> buffer;
        buffer.reserve(8 + mesh.number_of_vertices() * 12 + mesh.number_of_faces() * 12);

        // Create vertex index map
        std::map<Mesh::Vertex_index, uint32_t> vertex_map;
        uint32_t vertex_idx = 0;

        // Write vertex count
        write_uint32_le(buffer, static_cast<uint32_t>(mesh.number_of_vertices()));

        // Write vertices
        for (auto v : mesh.vertices()) {
            const Point_3& p = mesh.point(v);
            write_float_le(buffer, static_cast<float>(p.x()));
            write_float_le(buffer, static_cast<float>(p.y()));
            write_float_le(buffer, static_cast<float>(p.z()));
            vertex_map[v] = vertex_idx++;
        }

        // Write face count
        write_uint32_le(buffer, static_cast<uint32_t>(mesh.number_of_faces()));

        // Write faces (triangles only)
        for (auto f : mesh.faces()) {
            std::vector<uint32_t> face_verts;
            for (auto v : vertices_around_face(mesh.halfedge(f), mesh)) {
                face_verts.push_back(vertex_map[v]);
            }

            // Ensure triangle
            if (face_verts.size() != 3) {
                throw std::runtime_error("Mesh contains non-triangle face");
            }

            write_uint32_le(buffer, face_verts[0]);
            write_uint32_le(buffer, face_verts[1]);
            write_uint32_le(buffer, face_verts[2]);
        }

        return buffer;
    }

    Mesh deserialize_mesh_binary(const std::vector<uint8_t>& data, uint32_t expected_vertices, uint32_t expected_faces)
    {
        if (data.size() < 8) {
            throw std::runtime_error("Binary mesh data too small");
        }

        const uint8_t* ptr = data.data();
        size_t offset      = 0;

        // Read vertex count
        uint32_t vertex_count = read_uint32_le(ptr + offset);
        offset += 4;

        if (expected_vertices > 0 && vertex_count != expected_vertices) {
            throw std::runtime_error("Binary mesh vertex count mismatch");
        }

        const size_t vertices_block_size = static_cast<size_t>(vertex_count) * 12;
        const size_t face_count_offset   = offset + vertices_block_size;

        if (data.size() < face_count_offset + 4) {
            throw std::runtime_error("Binary mesh data truncated (vertices)");
        }

        uint32_t face_count = read_uint32_le(ptr + face_count_offset);

        if (expected_faces > 0 && face_count != expected_faces) {
            throw std::runtime_error("Binary mesh face count mismatch");
        }

        const size_t faces_block_size = static_cast<size_t>(face_count) * 12;
        const size_t expected_size    = 4 + vertices_block_size + 4 + faces_block_size;
        if (data.size() < expected_size) {
            throw std::runtime_error("Binary mesh data truncated (expected size)");
        }

        // Create mesh after both counts are known
        Mesh mesh;
        mesh.reserve(vertex_count, face_count * 3, face_count);
        std::vector<Mesh::Vertex_index> vertex_indices;
        vertex_indices.reserve(vertex_count);

        // Deserialize vertices
        offset = 4;
        for (uint32_t i = 0; i < vertex_count; ++i) {
            float x = read_float_le(ptr + offset);
            offset += 4;
            float y = read_float_le(ptr + offset);
            offset += 4;
            float z = read_float_le(ptr + offset);
            offset += 4;

            Point_3 p(x, y, z);
            vertex_indices.push_back(mesh.add_vertex(p));
        }

        // Move offset to faces block start (skip stored face_count)
        offset = face_count_offset + 4;

        // Read faces
        for (uint32_t i = 0; i < face_count; ++i) {
            uint32_t i0 = read_uint32_le(ptr + offset);
            offset += 4;
            uint32_t i1 = read_uint32_le(ptr + offset);
            offset += 4;
            uint32_t i2 = read_uint32_le(ptr + offset);
            offset += 4;

            if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) {
                throw std::runtime_error("Binary mesh face index out of range");
            }

            std::vector<Mesh::Vertex_index> face_verts = { vertex_indices[i0], vertex_indices[i1], vertex_indices[i2] };
            mesh.add_face(face_verts);
        }

        return mesh;
    }

    PolygonSoup deserialize_mesh_binary_to_soup(const std::vector<uint8_t>& data, uint32_t expected_vertices,
                                                uint32_t expected_faces)
    {
        if (data.size() < 8) {
            throw std::runtime_error("Binary mesh data too small");
        }

        const uint8_t* ptr = data.data();
        size_t offset      = 0;

        // Read vertex count
        uint32_t vertex_count = read_uint32_le(ptr + offset);
        offset += 4;

        if (expected_vertices > 0 && vertex_count != expected_vertices) {
            throw std::runtime_error("Binary mesh vertex count mismatch");
        }

        const size_t vertices_block_size = static_cast<size_t>(vertex_count) * 12;
        const size_t face_count_offset   = offset + vertices_block_size;

        if (data.size() < face_count_offset + 4) {
            throw std::runtime_error("Binary mesh data truncated (vertices)");
        }

        uint32_t face_count = read_uint32_le(ptr + face_count_offset);

        if (expected_faces > 0 && face_count != expected_faces) {
            throw std::runtime_error("Binary mesh face count mismatch");
        }

        const size_t faces_block_size = static_cast<size_t>(face_count) * 12;
        const size_t expected_size    = 4 + vertices_block_size + 4 + faces_block_size;
        if (data.size() < expected_size) {
            throw std::runtime_error("Binary mesh data truncated (expected size)");
        }

        PolygonSoup soup;
        soup.points.reserve(vertex_count);
        soup.polygons.reserve(face_count);

        // Deserialize vertices
        offset = 4;
        for (uint32_t i = 0; i < vertex_count; ++i) {
            float x = read_float_le(ptr + offset);
            offset += 4;
            float y = read_float_le(ptr + offset);
            offset += 4;
            float z = read_float_le(ptr + offset);
            offset += 4;

            soup.points.emplace_back(static_cast<double>(x), static_cast<double>(y), static_cast<double>(z));
        }

        // Move offset to faces block start (skip stored face_count)
        offset = face_count_offset + 4;

        // Read faces
        for (uint32_t i = 0; i < face_count; ++i) {
            uint32_t i0 = read_uint32_le(ptr + offset);
            offset += 4;
            uint32_t i1 = read_uint32_le(ptr + offset);
            offset += 4;
            uint32_t i2 = read_uint32_le(ptr + offset);
            offset += 4;

            if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) {
                throw std::runtime_error("Binary mesh face index out of range");
            }

            soup.polygons.push_back(
                { static_cast<std::size_t>(i0), static_cast<std::size_t>(i1), static_cast<std::size_t>(i2) });
        }

        soup.load_time_ms = 0.0;
        return soup;
    }

    std::string base64_encode(const std::vector<uint8_t>& data)
    {
        std::string encoded;
        encoded.reserve(((data.size() + 2) / 3) * 4);

        int val  = 0;
        int valb = -6;
        for (uint8_t c : data) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                encoded.push_back(base64_chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }

        if (valb > -6) {
            encoded.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
        }

        while (encoded.size() % 4) {
            encoded.push_back('=');
        }

        return encoded;
    }

    std::vector<uint8_t> base64_decode(const std::string& encoded)
    {
        // Build decode table
        static int decode_table[256];
        static bool table_built = false;

        if (!table_built) {
            std::fill(std::begin(decode_table), std::end(decode_table), -1);
            for (int i = 0; i < 64; ++i) {
                decode_table[static_cast<unsigned char>(base64_chars[i])] = i;
            }
            table_built = true;
        }

        std::vector<uint8_t> decoded;
        decoded.reserve((encoded.size() / 4) * 3);

        int val  = 0;
        int valb = -8;

        for (char c : encoded) {
            if (c == '=')
                break;

            int idx = decode_table[static_cast<unsigned char>(c)];
            if (idx == -1) {
                throw std::runtime_error("Invalid base64 character");
            }

            val = (val << 6) + idx;
            valb += 6;

            if (valb >= 0) {
                decoded.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }

        return decoded;
    }

}  // namespace Engine
}  // namespace MeshRepair
