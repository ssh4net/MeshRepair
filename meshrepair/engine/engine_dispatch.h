#pragma once

#include "engine_wrapper.h"
#include "protocol.h"
#include <nlohmann/json.hpp>

namespace MeshRepair {
namespace Engine {

    // Procedural dispatcher entry (bypasses CommandHandler class)
    nlohmann::json dispatch_command_procedural(EngineWrapper& engine, const nlohmann::json& cmd, bool verbose,
                                               bool show_stats, bool socket_mode);

}  // namespace Engine
}  // namespace MeshRepair
