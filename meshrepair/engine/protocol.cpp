#include "protocol.h"
#include <cstring>
#include <stdexcept>
#include <sstream>

namespace MeshRepair {
namespace Engine {

    // Helper: Read exact N bytes from stream
    static void read_exact(std::istream& stream, char* buffer, size_t n)
    {
        stream.read(buffer, n);
        if (!stream || stream.gcount() != static_cast<std::streamsize>(n)) {
            throw std::runtime_error("Failed to read from stream (connection closed or I/O error)");
        }
    }

    // Helper: Convert 4 bytes to uint32_t (little-endian)
    static uint32_t bytes_to_uint32_le(const char* bytes)
    {
        return static_cast<uint32_t>(static_cast<uint8_t>(bytes[0]))
               | (static_cast<uint32_t>(static_cast<uint8_t>(bytes[1])) << 8)
               | (static_cast<uint32_t>(static_cast<uint8_t>(bytes[2])) << 16)
               | (static_cast<uint32_t>(static_cast<uint8_t>(bytes[3])) << 24);
    }

    // Helper: Convert uint32_t to 4 bytes (little-endian)
    static void uint32_to_bytes_le(uint32_t value, char* bytes)
    {
        bytes[0] = static_cast<char>(value & 0xFF);
        bytes[1] = static_cast<char>((value >> 8) & 0xFF);
        bytes[2] = static_cast<char>((value >> 16) & 0xFF);
        bytes[3] = static_cast<char>((value >> 24) & 0xFF);
    }

    nlohmann::json read_message(std::istream& stream, MessageType* out_type)
    {
        // Read header: [length:4][type:1]
        char header[HEADER_SIZE_BYTES];
        read_exact(stream, header, HEADER_SIZE_BYTES);

        // Parse length
        uint32_t payload_length = bytes_to_uint32_le(header);

        // Validate length
        if (payload_length == 0) {
            throw std::runtime_error("Invalid message: payload length is 0");
        }
        if (payload_length > MAX_MESSAGE_SIZE) {
            std::ostringstream oss;
            oss << "Invalid message: payload length " << payload_length << " exceeds maximum " << MAX_MESSAGE_SIZE;
            throw std::runtime_error(oss.str());
        }

        // Parse type
        MessageType msg_type = static_cast<MessageType>(static_cast<uint8_t>(header[4]));
        if (out_type) {
            *out_type = msg_type;
        }

        // Validate type
        if (msg_type != MessageType::COMMAND && msg_type != MessageType::RESPONSE && msg_type != MessageType::EVENT) {
            std::ostringstream oss;
            oss << "Invalid message type: 0x" << std::hex << static_cast<int>(static_cast<uint8_t>(header[4]));
            throw std::runtime_error(oss.str());
        }

        // Read payload
        std::string payload(payload_length, '\0');
        read_exact(stream, &payload[0], payload_length);

        // Parse JSON
        try {
            return nlohmann::json::parse(payload);
        } catch (const nlohmann::json::parse_error& ex) {
            std::ostringstream oss;
            oss << "JSON parse error: " << ex.what();
            throw std::runtime_error(oss.str());
        }
    }

    void write_message(std::ostream& stream, const nlohmann::json& msg, MessageType type)
    {
        // Serialize JSON to string
        std::string payload     = msg.dump();
        uint32_t payload_length = static_cast<uint32_t>(payload.size());

        // Validate length
        if (payload_length > MAX_MESSAGE_SIZE) {
            std::ostringstream oss;
            oss << "Message too large: " << payload_length << " bytes exceeds maximum " << MAX_MESSAGE_SIZE;
            throw std::runtime_error(oss.str());
        }

        // Build header: [length:4][type:1]
        char header[HEADER_SIZE_BYTES];
        uint32_to_bytes_le(payload_length, header);
        header[4] = static_cast<char>(static_cast<uint8_t>(type));

        // Write header + payload
        stream.write(header, HEADER_SIZE_BYTES);
        stream.write(payload.c_str(), payload_length);
        stream.flush();

        if (!stream) {
            throw std::runtime_error("Failed to write to stream (I/O error)");
        }
    }

    nlohmann::json create_success_response(const std::string& message)
    {
        nlohmann::json resp;
        resp["type"] = "success";
        if (!message.empty()) {
            resp["message"] = message;
        }
        return resp;
    }

    nlohmann::json create_error_response(const std::string& error_message, const std::string& error_type)
    {
        nlohmann::json resp;
        resp["type"]  = "error";
        resp["error"] = { { "type", error_type }, { "message", error_message } };
        return resp;
    }

    nlohmann::json create_progress_event(double progress, const std::string& status)
    {
        nlohmann::json event;
        event["type"]     = "progress";
        event["progress"] = progress;
        if (!status.empty()) {
            event["status"] = status;
        }
        return event;
    }

    nlohmann::json create_log_event(const std::string& level, const std::string& message)
    {
        nlohmann::json event;
        event["type"]    = "log";
        event["level"]   = level;
        event["message"] = message;
        return event;
    }

    bool validate_command(const nlohmann::json& cmd, const std::string& expected_command, std::string* error_msg)
    {
        // Check if it's an object
        if (!cmd.is_object()) {
            if (error_msg) {
                *error_msg = "Command must be a JSON object";
            }
            return false;
        }

        // Check for 'command' field
        if (!cmd.contains("command")) {
            if (error_msg) {
                *error_msg = "Missing 'command' field";
            }
            return false;
        }

        // Check command type
        if (!cmd["command"].is_string()) {
            if (error_msg) {
                *error_msg = "'command' field must be a string";
            }
            return false;
        }

        // Check command name
        std::string cmd_name = cmd["command"].get<std::string>();
        if (cmd_name != expected_command) {
            if (error_msg) {
                *error_msg = "Expected command '" + expected_command + "', got '" + cmd_name + "'";
            }
            return false;
        }

        return true;
    }

}  // namespace Engine
}  // namespace MeshRepair
