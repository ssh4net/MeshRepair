#include "include/debug_path.h"

#include <filesystem>

namespace MeshRepair {
namespace DebugPath {

namespace {
    std::string g_base_directory;

    std::string normalized(const std::string& path)
    {
        if (path.empty()) {
            return {};
        }

#if defined(_WIN32)
        constexpr char kSep = '\\';
#else
        constexpr char kSep = '/';
#endif

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
}

const std::string& get_base_directory()
{
    return g_base_directory;
}

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

}  // namespace DebugPath
}  // namespace MeshRepair

