#pragma once

#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <cstdint>

namespace MeshRepair {
namespace Engine {

    // Binary protocol message format:
    // [length:4 bytes][type:1 byte][json_payload:N bytes]
    //
    // length: uint32_t (little-endian) - size of json_payload in bytes
    // type: uint8_t - message type (0x01 = command, 0x02 = response, 0x03 = event)
    // json_payload: UTF-8 encoded JSON string

    enum class MessageType : uint8_t {
        COMMAND  = 0x01,  // Command from client (Blender addon)
        RESPONSE = 0x02,  // Response from engine
        EVENT    = 0x03   // Async event (progress, log, etc.)
    };

    // Protocol constants
    constexpr size_t HEADER_SIZE_BYTES  = 5;                  // 4 (length) + 1 (type)
    constexpr uint32_t MAX_MESSAGE_SIZE = 100 * 1024 * 1024;  // 100 MB max

    // Read a complete message from stream
    // Returns: parsed JSON object
    // Throws: std::runtime_error on protocol errors or I/O errors
    nlohmann::json read_message(std::istream& stream, MessageType* out_type = nullptr);

    // Write a message to stream
    // Parameters:
    //   stream: output stream (typically stdout)
    //   msg: JSON object to send
    //   type: message type (default: RESPONSE)
    // Throws: std::runtime_error on I/O errors
    void write_message(std::ostream& stream, const nlohmann::json& msg, MessageType type = MessageType::RESPONSE);

    // Helper: Create success response
    nlohmann::json create_success_response(const std::string& message = "");

    // Helper: Create error response
    nlohmann::json create_error_response(const std::string& error_message, const std::string& error_type = "error");

    // Helper: Create progress event
    nlohmann::json create_progress_event(double progress, const std::string& status = "");

    // Helper: Create log event
    nlohmann::json create_log_event(const std::string& level, const std::string& message);

    // Helper: Validate command structure
    bool validate_command(const nlohmann::json& cmd, const std::string& expected_command,
                          std::string* error_msg = nullptr);

}  // namespace Engine
}  // namespace MeshRepair
