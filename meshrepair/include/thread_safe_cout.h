#ifndef MESHREPAIR_THREAD_SAFE_COUT_H
#define MESHREPAIR_THREAD_SAFE_COUT_H

#include <string>
#include <string_view>
#include "logger.h"

namespace MeshRepair {

inline void
thread_safe_log(const std::string_view msg)
{
    logInfo(LogCategory::Fill, std::string(msg));
}

inline void
thread_safe_log_err(const std::string_view msg)
{
    logError(LogCategory::Fill, std::string(msg));
}

}  // namespace MeshRepair

#endif  // MESHREPAIR_THREAD_SAFE_COUT_H
