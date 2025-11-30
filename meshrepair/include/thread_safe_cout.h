#ifndef MESHREPAIR_THREAD_SAFE_COUT_H
#define MESHREPAIR_THREAD_SAFE_COUT_H

#include <cstdio>
#include <mutex>
#include <string_view>

namespace MeshRepair {

// Minimal thread-safe logging helpers (C-style)
inline std::mutex&
thread_safe_log_mutex()
{
    static std::mutex m;
    return m;
}

inline void
thread_safe_log(const std::string_view msg)
{
    std::lock_guard<std::mutex> lock(thread_safe_log_mutex());
    std::fwrite(msg.data(), 1, msg.size(), stdout);
    std::fflush(stdout);
}

inline void
thread_safe_log_err(const std::string_view msg)
{
    std::lock_guard<std::mutex> lock(thread_safe_log_mutex());
    std::fwrite(msg.data(), 1, msg.size(), stderr);
    std::fflush(stderr);
}

}  // namespace MeshRepair

#endif  // MESHREPAIR_THREAD_SAFE_COUT_H
