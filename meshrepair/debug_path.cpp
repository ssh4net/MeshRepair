#include "include/debug_path.h"

#include <atomic>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace MeshRepair {
namespace DebugPath {

    namespace {
        std::string g_base_directory;
        std::atomic<uint32_t> g_debug_counter { 0 };
        std::atomic<uint32_t> g_step_counter { 0 };

        std::string normalized(const std::string& path)
        {
            if (path.empty()) {
                return {};
            }

            std::filesystem::path fs_path(path);
            auto normalized_path = fs_path.make_preferred().string();

            if (!normalized_path.empty() && (normalized_path.back() == '\\' || normalized_path.back() == '/')) {
                normalized_path.pop_back();
            }

            return normalized_path;
        }
    }  // namespace

    void set_base_directory(const std::string& path)
    {
        g_base_directory = normalized(path);
        if (!g_base_directory.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(g_base_directory, ec);
        }
        g_debug_counter.store(0);
        g_step_counter.store(0);
    }

    const std::string& get_base_directory() { return g_base_directory; }

    std::string resolve(const std::string& filename)
    {
        if (filename.empty()) {
            return filename;
        }

        std::filesystem::path file_path(filename);
        if (file_path.is_absolute()) {
            return file_path.make_preferred().string();
        }

        if (g_base_directory.empty()) {
            return file_path.make_preferred().string();
        }

        std::filesystem::path base_path(g_base_directory);
        auto combined = (base_path / file_path).make_preferred();
        return combined.string();
    }

    std::string next_debug_filename(const std::string& label, const std::string& extension)
    {
        std::ostringstream oss;
        oss << "debug_" << std::setw(3) << std::setfill('0') << g_debug_counter.fetch_add(1);
        if (!label.empty()) {
            oss << "_" << label;
        }
        if (!extension.empty()) {
            if (extension.front() == '.') {
                oss << extension;
            } else {
                oss << "." << extension;
            }
        }
        return resolve(oss.str());
    }

    std::string start_step(const std::string& label)
    {
        unsigned int step_index = g_step_counter.fetch_add(1);
        std::ostringstream oss;
        oss << "debug_" << std::setw(2) << std::setfill('0') << step_index;
        if (!label.empty()) {
            oss << "_" << label;
        }
        return resolve(oss.str());
    }

    std::string step_file(const std::string& label, const std::string& extension)
    {
        std::string base = start_step(label);
        if (!extension.empty()) {
            if (extension.front() == '.') {
                base += extension;
            } else {
                base += "." + extension;
            }
        }
        return base;
    }

}  // namespace DebugPath
}  // namespace MeshRepair
