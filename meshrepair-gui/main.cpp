#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <chrono>
#include <deque>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GL/glew.h>

#include "dnd_glfw.h"

#include "mr_gui.h"

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl3.h"
#include "misc/cpp/imgui_stdlib.h"

#include <nfd.h>

#if defined(_WIN32)
#    include <windows.h>
#elif defined(__linux__) || defined(__unix__)
#    define GLFW_EXPOSE_NATIVE_X11
#    include <X11/Xlib.h>
#    include <GLFW/glfw3native.h>
#endif
#ifdef Success
#    undef Success
#endif

#include "config.h"
#include "local_batch_queue.h"
#include "logger.h"

namespace fs = std::filesystem;
using MeshRepair::CompletedJob;
using MeshRepair::RepairJobConfig;
using MeshRepair::RepairJobStatus;
using MeshRepair::RepairQueue;
using MeshRepair::RepairQueueConfig;

enum class ThemeMode { MrLight = 0, MrDark = 1, ImGuiLight = 2, ImGuiDark = 3 };

struct GuiOptions {
    std::string inputPath;
    std::string outputPath;
    std::string outputPostfix        = "_repaired";
    int outputFormat                 = 1;  // 0=OBJ, 1=PLY, 2=OFF (auto mode)
    int windowWidth                  = 860;
    int windowHeight                 = 760;
    int verbosity                    = 1;
    bool validate                    = false;
    bool asciiPly                    = false;
    bool perHoleInfo                 = false;
    bool enablePreprocessing         = true;
    bool preprocessRemoveDuplicates  = true;
    bool preprocessRemoveNonManifold = true;
    bool preprocessRemove3FaceFans   = true;
    bool preprocessRemoveIsolated    = true;
    bool preprocessKeepLargest       = true;
    int nonManifoldPasses            = 10;
    bool preprocessRemoveLongEdges   = false;
    double preprocessMaxEdgeRatio    = 0.125;
    int numThreads                   = 0;
    int queueSize                    = 10;
    bool usePartitioned              = true;
    bool forceCgalLoader             = false;
    int continuity                   = static_cast<int>(MeshRepair::Config::DEFAULT_FAIRING_CONTINUITY);
    int maxBoundary                  = static_cast<int>(MeshRepair::Config::DEFAULT_MAX_HOLE_BOUNDARY);
    double maxDiameterRatio          = MeshRepair::Config::DEFAULT_MAX_HOLE_DIAMETER_RATIO;
    bool use2dCdt                    = MeshRepair::Config::DEFAULT_USE_2D_CDT;
    bool use3dDelaunay               = MeshRepair::Config::DEFAULT_USE_3D_DELAUNAY;
    bool skipCubicSearch             = MeshRepair::Config::DEFAULT_SKIP_CUBIC;
    bool refine                      = MeshRepair::Config::DEFAULT_REFINE;
    int minPartitionEdges            = static_cast<int>(MeshRepair::Config::DEFAULT_MIN_PARTITION_EDGES);
    bool holesOnly                   = false;
    std::string tempDir;
    int timeoutSeconds = 0;  // 0 = no timeout
    bool colorOutput   = true;
    std::string procName;
};

constexpr int kMinWindowWidth      = 320;
constexpr int kMinWindowHeight     = 240;
constexpr double kMemoryMultiplier = 2.0;  // Rough estimate of working set vs. file size

struct BatchJob {
    uint64_t id = 0;
    std::string inputPath;
    std::string outputPath;
    double sizeGb = 0.0;
    std::shared_ptr<std::atomic<bool>> cancel_flag;
};

struct AppState {
    GuiOptions options;
    std::string statusMessage;
    std::vector<std::string> pendingDrops;
    std::mutex dropMutex;
    bool nfdReady   = false;
    bool darkTheme  = false;
    ThemeMode theme = ThemeMode::MrLight;
    bool autoMode   = true;
    std::string imguiIniData;
    std::deque<std::string> batchQueue;
    std::vector<BatchJob> activeJobs;
    int completedJobs      = 0;
    int failedJobs         = 0;
    int parallelJobs       = 4;
    int ramLimitGb         = 16;
    bool recursiveInput    = false;
    bool startRequested    = false;
    bool paused            = false;
    bool cancelRequested   = false;
    bool dragOverlayActive = false;
    RepairQueue repairQueue;
    bool queueStarted      = false;
    size_t queueWorkers    = 0;
    bool batchTimingActive = false;
    std::chrono::steady_clock::time_point batchStartTime;
    double lastBatchTotalTimeMs = 0.0;
    std::deque<double> jobDurationHistoryMs;
};

struct DisableGuard {
    bool active { false };

    explicit DisableGuard(bool shouldDisable)
        : active(shouldDisable)
    {
        if (active) {
            ImGui::BeginDisabled();
        }
    }

    ~DisableGuard()
    {
        if (active) {
            ImGui::EndDisabled();
        }
    }
};

struct FloatingScope {
    GLFWwindow* window         = nullptr;
    int previousFloatingAttrib = GLFW_FALSE;

    FloatingScope(GLFWwindow* w, bool dropFloating)
        : window(w)
    {
        if (window) {
            previousFloatingAttrib = glfwGetWindowAttrib(window, GLFW_FLOATING);
            if (dropFloating && previousFloatingAttrib == GLFW_TRUE) {
                glfwSetWindowAttrib(window, GLFW_FLOATING, GLFW_FALSE);
            }
        }
    }

    ~FloatingScope()
    {
        if (window && previousFloatingAttrib == GLFW_TRUE) {
            glfwSetWindowAttrib(window, GLFW_FLOATING, GLFW_TRUE);
        }
    }
};

static void
glfwErrorCallback(int error, const char* description)
{
    fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

static std::string
deriveOutputPath(const std::string& inputPath, const std::string& postfix)
{
    fs::path input   = fs::path(inputPath);
    fs::path dir     = input.parent_path();
    std::string stem = input.stem().string();
    std::string ext  = input.extension().string();
    if (stem.empty()) {
        stem = "output";
    }
    fs::path outName = fs::path(stem + postfix + ext);
    return (dir / outName).string();
}

static std::string
findFontPath(const char* argv0)
{
    std::vector<fs::path> candidates;
#ifdef MESHREPAIR_GUI_FONT_PATH
    candidates.emplace_back(fs::path(MESHREPAIR_GUI_FONT_PATH));
#endif

    fs::path exeDir = fs::current_path();
    if (argv0 && argv0[0] != '\0') {
        std::error_code ec;
        fs::path exePath = fs::weakly_canonical(fs::path(argv0), ec);
        if (!ec && !exePath.empty()) {
            exeDir = exePath.parent_path();
        }
    }

    candidates.emplace_back(exeDir / "fonts" / "FiraSans-Medium.otf");
    candidates.emplace_back(exeDir / ".." / "share" / "meshrepair-gui" / "fonts" / "FiraSans-Medium.otf");

    for (const auto& c : candidates) {
        std::error_code ec;
        if (!c.empty() && fs::exists(c, ec)) {
            return c.string();
        }
    }

    return {};
}

static std::string
toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

static std::string
formatSeconds(int totalSeconds)
{
    if (totalSeconds <= 0) {
        return "0s";
    }

    int hours   = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;

    std::ostringstream oss;
    if (hours > 0) {
        oss << hours << "h";
        if (minutes > 0) {
            oss << " " << minutes << "m";
        }
    } else if (minutes > 0) {
        oss << minutes << "m";
    }

    if (seconds > 0 && (hours == 0 || minutes == 0 || hours < 3)) {
        if (hours > 0 || minutes > 0) {
            oss << " ";
        }
        oss << seconds << "s";
    }

    return oss.str();
}

static bool
themeModeIsDark(ThemeMode mode)
{
    return mode == ThemeMode::MrDark || mode == ThemeMode::ImGuiDark;
}

static ThemeMode
themeModeFromString(const std::string& value, ThemeMode fallback)
{
    std::string lower = toLower(value);
    if (lower == "mrdark" || lower == "dark") {
        return ThemeMode::MrDark;
    }
    if (lower == "mrlight" || lower == "light") {
        return ThemeMode::MrLight;
    }
    if (lower == "imguidark") {
        return ThemeMode::ImGuiDark;
    }
    if (lower == "imguilight") {
        return ThemeMode::ImGuiLight;
    }
    return fallback;
}

static const char*
themeModeToString(ThemeMode mode)
{
    switch (mode) {
    case ThemeMode::MrDark: return "MrDark";
    case ThemeMode::MrLight: return "MrLight";
    case ThemeMode::ImGuiDark: return "ImGuiDark";
    case ThemeMode::ImGuiLight: return "ImGuiLight";
    }
    return "MrLight";
}

static bool
splitKeyValue(const std::string& line, std::string& key, std::string& value)
{
    auto pos = line.find('=');
    if (pos == std::string::npos) {
        return false;
    }
    key   = line.substr(0, pos);
    value = line.substr(pos + 1);
    return true;
}

static bool
parseInt(const std::string& value, int& out)
{
    try {
        out = std::stoi(value);
        return true;
    } catch (...) {
        return false;
    }
}

static bool
parseDouble(const std::string& value, double& out)
{
    try {
        out = std::stod(value);
        return true;
    } catch (...) {
        return false;
    }
}

static bool
parseBool(const std::string& value)
{
    return value == "1" || toLower(value) == "true";
}

static std::string
configFilePath()
{
    return "meshrepair_gui.ini";
}

static bool
isSupportedMeshFile(const fs::path& path);

static double
fileSizeGb(const fs::path& path)
{
    std::error_code ec;
    auto size = fs::file_size(path, ec);
    if (ec) {
        return 0.0;
    }
    double gb = static_cast<double>(size) / (1024.0 * 1024.0 * 1024.0);
    return std::max(gb, 0.0);
}

static std::string
resolveOutputPath(const std::string& inputPath, const std::string& userOutput, const std::string& postfix,
                  int outputFormat)
{
    if (userOutput.empty()) {
        // Auto mode: derive base path, then override extension according to format selector
        std::string derived = deriveOutputPath(inputPath, postfix);
        fs::path out(derived);
        switch (outputFormat) {
        case 0: out.replace_extension(".obj"); break;
        case 1: out.replace_extension(".ply"); break;
        case 2: out.replace_extension(".off"); break;
        default: break;
        }
        return out.string();
    }

    fs::path outPath(userOutput);
    std::error_code ec;
    if (fs::is_directory(outPath, ec)) {
        fs::path input   = fs::path(inputPath);
        std::string stem = input.stem().string();
        std::string ext  = input.extension().string();

        fs::path outName = fs::path(stem + postfix + ext);
        fs::path full    = outPath / outName;

        switch (outputFormat) {
        case 0: full.replace_extension(".obj"); break;
        case 1: full.replace_extension(".ply"); break;
        case 2: full.replace_extension(".off"); break;
        default: break;
        }

        return full.string();
    }

    return userOutput;
}

static std::vector<std::string>
expandPaths(const std::vector<std::string>& inputs, bool recursive)
{
    std::set<std::string> uniquePaths;
    for (const auto& raw : inputs) {
        fs::path p(raw);
        std::error_code ec;
        if (!fs::exists(p, ec)) {
            continue;
        }

        if (fs::is_directory(p, ec)) {
            if (recursive) {
                for (const auto& entry : fs::recursive_directory_iterator(p, ec)) {
                    if (ec) {
                        break;
                    }
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    if (isSupportedMeshFile(entry.path())) {
                        uniquePaths.insert(entry.path().string());
                    }
                }
            } else {
                for (const auto& entry : fs::directory_iterator(p, ec)) {
                    if (ec) {
                        break;
                    }
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    if (isSupportedMeshFile(entry.path())) {
                        uniquePaths.insert(entry.path().string());
                    }
                }
            }
        } else if (isSupportedMeshFile(p)) {
            uniquePaths.insert(p.string());
        }
    }

    return std::vector<std::string>(uniquePaths.begin(), uniquePaths.end());
}

static bool
isSupportedMeshFile(const fs::path& path)
{
    std::string ext                                       = toLower(path.extension().string());
    static const std::array<std::string, 3> kSupportedExt = { ".obj", ".ply", ".off" };
    bool supported = std::find(kSupportedExt.begin(), kSupportedExt.end(), ext) != kSupportedExt.end();
    if (!supported) {
        fprintf(stderr, "Unsupported file extension: %s (%s)\n", ext.c_str(), path.string().c_str());
    }
    return supported;
}

static void
loadAppConfig(AppState& app, const std::string& path)
{
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        return;
    }

    std::ifstream file(path);
    if (!file) {
        fprintf(stderr, "Failed to open config at %s\n", path.c_str());
        return;
    }

    enum class Section { Unknown, App, ImGui };
    Section section      = Section::Unknown;
    bool themeConfigured = false;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line == "[App]") {
            section = Section::App;
            continue;
        }
        if (line == "[ImGui]") {
            section = Section::ImGui;
            continue;
        }

        if (section == Section::App) {
            std::string key;
            std::string value;
            if (!splitKeyValue(line, key, value)) {
                continue;
            }

            int intVal       = 0;
            double doubleVal = 0.0;

            if (key == "Verbosity" && parseInt(value, intVal)) {
                app.options.verbosity = intVal;
            } else if (key == "Theme") {
                app.theme       = themeModeFromString(value, app.theme);
                app.darkTheme   = themeModeIsDark(app.theme);
                themeConfigured = true;
            } else if (key == "Validate") {
                app.options.validate = parseBool(value);
            } else if (key == "AsciiPly") {
                app.options.asciiPly = parseBool(value);
            } else if (key == "PerHoleInfo") {
                app.options.perHoleInfo = parseBool(value);
            } else if (key == "EnablePreprocessing") {
                app.options.enablePreprocessing = parseBool(value);
            } else if (key == "RemoveDuplicates") {
                app.options.preprocessRemoveDuplicates = parseBool(value);
            } else if (key == "RemoveNonManifold") {
                app.options.preprocessRemoveNonManifold = parseBool(value);
            } else if (key == "Remove3FaceFans") {
                app.options.preprocessRemove3FaceFans = parseBool(value);
            } else if (key == "RemoveIsolated") {
                app.options.preprocessRemoveIsolated = parseBool(value);
            } else if (key == "KeepLargest") {
                app.options.preprocessKeepLargest = parseBool(value);
            } else if (key == "RemoveLongEdges") {
                app.options.preprocessRemoveLongEdges = parseBool(value);
            } else if (key == "MaxEdgeRatio" && parseDouble(value, doubleVal)) {
                app.options.preprocessMaxEdgeRatio = doubleVal;
            } else if (key == "NonManifoldPasses" && parseInt(value, intVal)) {
                app.options.nonManifoldPasses = intVal;
            } else if (key == "NumThreads" && parseInt(value, intVal)) {
                app.options.numThreads = intVal;
            } else if (key == "QueueSize" && parseInt(value, intVal)) {
                app.options.queueSize = intVal;
            } else if (key == "UsePartitioned") {
                app.options.usePartitioned = parseBool(value);
            } else if (key == "ForceCgalLoader") {
                app.options.forceCgalLoader = parseBool(value);
            } else if (key == "Continuity" && parseInt(value, intVal)) {
                app.options.continuity = intVal;
            } else if (key == "MaxBoundary" && parseInt(value, intVal)) {
                app.options.maxBoundary = intVal;
            } else if (key == "MaxDiameterRatio" && parseDouble(value, doubleVal)) {
                app.options.maxDiameterRatio = doubleVal;
            } else if (key == "Use2dCdt") {
                app.options.use2dCdt = parseBool(value);
            } else if (key == "Use3dDelaunay") {
                app.options.use3dDelaunay = parseBool(value);
            } else if (key == "SkipCubicSearch") {
                app.options.skipCubicSearch = parseBool(value);
            } else if (key == "Refine") {
                app.options.refine = parseBool(value);
            } else if (key == "MinPartitionEdges" && parseInt(value, intVal)) {
                app.options.minPartitionEdges = intVal;
            } else if (key == "HolesOnly") {
                app.options.holesOnly = parseBool(value);
            } else if (key == "TempDir") {
                app.options.tempDir = value;
            } else if (key == "TimeoutSeconds" && parseInt(value, intVal)) {
                app.options.timeoutSeconds = intVal;
            } else if (key == "DarkTheme") {
                app.darkTheme = parseBool(value);
                if (!themeConfigured) {
                    app.theme = app.darkTheme ? ThemeMode::ImGuiDark : ThemeMode::ImGuiLight;
                }
            } else if (key == "AutoMode") {
                app.autoMode = parseBool(value);
            } else if (key == "WindowWidth" && parseInt(value, intVal)) {
                app.options.windowWidth = intVal;
            } else if (key == "WindowHeight" && parseInt(value, intVal)) {
                app.options.windowHeight = intVal;
            } else if (key == "ParallelJobs" && parseInt(value, intVal)) {
                app.parallelJobs = intVal;
            } else if (key == "RamLimitGb" && parseInt(value, intVal)) {
                app.ramLimitGb = intVal;
            } else if (key == "RecursiveInput") {
                app.recursiveInput = parseBool(value);
            } else if (key == "OutputPath") {
                app.options.outputPath = value;
            } else if (key == "OutputPostfix") {
                app.options.outputPostfix = value;
            } else if (key == "OutputFormat" && parseInt(value, intVal)) {
                app.options.outputFormat = intVal;
            }
        } else if (section == Section::ImGui) {
            app.imguiIniData.append(line);
            app.imguiIniData.push_back('\n');
        }
    }

    app.darkTheme = themeModeIsDark(app.theme);
}

static void
saveAppConfig(const AppState& app, const ImGuiIO& io, const std::string& path)
{
    (void)io;
    size_t iniSize            = 0;
    const char* imguiIni      = ImGui::SaveIniSettingsToMemory(&iniSize);
    std::string imguiIniBlock = imguiIni ? std::string(imguiIni, iniSize) : std::string();

    std::ofstream file(path, std::ios::trunc);
    if (!file) {
        fprintf(stderr, "Failed to write config to %s\n", path.c_str());
        return;
    }

    file << "[App]\n";
    file << "Verbosity=" << app.options.verbosity << "\n";
    file << "Theme=" << themeModeToString(app.theme) << "\n";
    file << "Validate=" << (app.options.validate ? 1 : 0) << "\n";
    file << "AsciiPly=" << (app.options.asciiPly ? 1 : 0) << "\n";
    file << "PerHoleInfo=" << (app.options.perHoleInfo ? 1 : 0) << "\n";
    file << "EnablePreprocessing=" << (app.options.enablePreprocessing ? 1 : 0) << "\n";
    file << "RemoveDuplicates=" << (app.options.preprocessRemoveDuplicates ? 1 : 0) << "\n";
    file << "RemoveNonManifold=" << (app.options.preprocessRemoveNonManifold ? 1 : 0) << "\n";
    file << "Remove3FaceFans=" << (app.options.preprocessRemove3FaceFans ? 1 : 0) << "\n";
    file << "RemoveIsolated=" << (app.options.preprocessRemoveIsolated ? 1 : 0) << "\n";
    file << "KeepLargest=" << (app.options.preprocessKeepLargest ? 1 : 0) << "\n";
    file << "RemoveLongEdges=" << (app.options.preprocessRemoveLongEdges ? 1 : 0) << "\n";
    file << "MaxEdgeRatio=" << app.options.preprocessMaxEdgeRatio << "\n";
    file << "NonManifoldPasses=" << app.options.nonManifoldPasses << "\n";
    file << "NumThreads=" << app.options.numThreads << "\n";
    file << "QueueSize=" << app.options.queueSize << "\n";
    file << "UsePartitioned=" << (app.options.usePartitioned ? 1 : 0) << "\n";
    file << "ForceCgalLoader=" << (app.options.forceCgalLoader ? 1 : 0) << "\n";
    file << "Continuity=" << app.options.continuity << "\n";
    file << "MaxBoundary=" << app.options.maxBoundary << "\n";
    file << "MaxDiameterRatio=" << app.options.maxDiameterRatio << "\n";
    file << "Use2dCdt=" << (app.options.use2dCdt ? 1 : 0) << "\n";
    file << "Use3dDelaunay=" << (app.options.use3dDelaunay ? 1 : 0) << "\n";
    file << "SkipCubicSearch=" << (app.options.skipCubicSearch ? 1 : 0) << "\n";
    file << "Refine=" << (app.options.refine ? 1 : 0) << "\n";
    file << "MinPartitionEdges=" << app.options.minPartitionEdges << "\n";
    file << "HolesOnly=" << (app.options.holesOnly ? 1 : 0) << "\n";
    file << "TempDir=" << app.options.tempDir << "\n";
    file << "TimeoutSeconds=" << app.options.timeoutSeconds << "\n";
    file << "DarkTheme=" << (app.darkTheme ? 1 : 0) << "\n";
    file << "AutoMode=" << (app.autoMode ? 1 : 0) << "\n";
    file << "WindowWidth=" << app.options.windowWidth << "\n";
    file << "WindowHeight=" << app.options.windowHeight << "\n";
    file << "ParallelJobs=" << app.parallelJobs << "\n";
    file << "RamLimitGb=" << app.ramLimitGb << "\n";
    file << "RecursiveInput=" << (app.recursiveInput ? 1 : 0) << "\n";
    file << "OutputPath=" << app.options.outputPath << "\n";
    file << "OutputPostfix=" << app.options.outputPostfix << "\n";
    file << "OutputFormat=" << app.options.outputFormat << "\n";
    file << "\n";
    file << "[ImGui]\n";
    file << imguiIniBlock;
}

static void
applyTheme(AppState& app)
{
    switch (app.theme) {
    case ThemeMode::MrDark: StyleColorsMrDark(); break;
    case ThemeMode::MrLight: StyleColorsMrLight(); break;
    case ThemeMode::ImGuiDark: ImGui::StyleColorsDark(); break;
    case ThemeMode::ImGuiLight:
    default: ImGui::StyleColorsLight(); break;
    }

    app.darkTheme = themeModeIsDark(app.theme);
}

static RepairJobConfig
buildJobConfig(const GuiOptions& opts)
{
    RepairJobConfig cfg;
    cfg.input_path                            = opts.inputPath;
    cfg.output_path                           = opts.outputPath;
    cfg.enable_preprocessing                  = opts.enablePreprocessing;
    cfg.preprocess_opt.remove_duplicates      = opts.preprocessRemoveDuplicates;
    cfg.preprocess_opt.remove_non_manifold    = opts.preprocessRemoveNonManifold;
    cfg.preprocess_opt.remove_3_face_fans     = opts.preprocessRemove3FaceFans;
    cfg.preprocess_opt.remove_isolated        = opts.preprocessRemoveIsolated;
    cfg.preprocess_opt.keep_largest_component = opts.preprocessKeepLargest;
    cfg.preprocess_opt.non_manifold_passes    = static_cast<size_t>(std::max(opts.nonManifoldPasses, 1));
    cfg.preprocess_opt.remove_long_edges      = opts.preprocessRemoveLongEdges;
    cfg.preprocess_opt.long_edge_max_ratio    = opts.preprocessMaxEdgeRatio;
    cfg.preprocess_opt.verbose                = opts.verbosity >= 2;
    cfg.preprocess_opt.debug                  = opts.verbosity >= 4;

    cfg.filling_options.fairing_continuity           = static_cast<unsigned int>(std::max(opts.continuity, 0));
    cfg.filling_options.max_hole_boundary_vertices   = static_cast<size_t>(std::max(opts.maxBoundary, 1));
    cfg.filling_options.max_hole_diameter_ratio      = opts.maxDiameterRatio;
    cfg.filling_options.use_2d_cdt                   = opts.use2dCdt;
    cfg.filling_options.use_3d_delaunay              = opts.use3dDelaunay;
    cfg.filling_options.skip_cubic_search            = opts.skipCubicSearch;
    cfg.filling_options.refine                       = opts.refine;
    cfg.filling_options.min_partition_boundary_edges = static_cast<size_t>(std::max(opts.minPartitionEdges, 0));
    cfg.filling_options.holes_only                   = opts.holesOnly;
    cfg.filling_options.keep_largest_component       = opts.preprocessKeepLargest;
    cfg.filling_options.show_progress                = opts.verbosity > 0;
    cfg.filling_options.verbose                      = opts.verbosity >= 2;

    cfg.use_partitioned   = opts.usePartitioned;
    cfg.validate_input    = opts.validate;
    cfg.ascii_ply         = opts.asciiPly;
    cfg.force_cgal_loader = opts.forceCgalLoader;
    cfg.verbose           = opts.verbosity >= 2;
    cfg.debug_dump        = opts.verbosity >= 4;
    cfg.temp_dir          = opts.tempDir;
    if (opts.timeoutSeconds > 0) {
        cfg.timeout_ms = static_cast<double>(opts.timeoutSeconds) * 1000.0;
    } else {
        cfg.timeout_ms = 0.0;
    }
    cfg.thread_count = opts.numThreads > 0 ? static_cast<size_t>(opts.numThreads) : 0;
    cfg.queue_size   = opts.queueSize > 0 ? static_cast<size_t>(opts.queueSize) : 10;

    return cfg;
}

static const char*
jobStatusLabel(RepairJobStatus status)
{
    switch (status) {
    case RepairJobStatus::Ok: return "ok";
    case RepairJobStatus::LoadFailed: return "load failed";
    case RepairJobStatus::PreprocessFailed: return "preprocess failed";
    case RepairJobStatus::ValidationFailed: return "validation failed";
    case RepairJobStatus::ProcessFailed: return "process failed";
    case RepairJobStatus::SaveFailed: return "save failed";
    case RepairJobStatus::Cancelled: return "cancelled";
    case RepairJobStatus::InternalError: return "internal error";
    }
    return "unknown";
}

static void
ensureRepairQueue(AppState& app)
{
    size_t workerCount = static_cast<size_t>(std::max(app.parallelJobs, 1));
    if (app.queueStarted && app.queueWorkers == workerCount) {
        return;
    }
    if (app.queueStarted && app.queueWorkers != workerCount && !app.activeJobs.empty()) {
        return;
    }

    if (app.queueStarted) {
        repair_queue_shutdown(app.repairQueue);
    }

    RepairQueueConfig cfg;
    cfg.worker_threads = workerCount;
    cfg.capacity       = std::max<size_t>(workerCount * 2, 2);
    repair_queue_init(app.repairQueue, cfg);
    app.queueStarted = true;
    app.queueWorkers = workerCount;
}

static bool
hasActiveJobs(const AppState& app)
{
    return !app.activeJobs.empty();
}

static void
enqueuePaths(AppState& app, const std::vector<std::string>& paths)
{
    auto expanded = expandPaths(paths, app.recursiveInput);
    if (expanded.empty()) {
        app.statusMessage = "No supported mesh files to queue.";
        return;
    }

    if (!hasActiveJobs(app) && app.batchQueue.empty()) {
        app.completedJobs        = 0;
        app.failedJobs           = 0;
        app.cancelRequested      = false;
        app.startRequested       = false;
        app.paused               = false;
        app.batchTimingActive    = false;
        app.lastBatchTotalTimeMs = 0.0;
        app.jobDurationHistoryMs.clear();
    }

    for (const auto& p : expanded) {
        app.batchQueue.push_back(p);
    }

    app.statusMessage = "Queued " + std::to_string(expanded.size()) + " file(s).";
    if (app.autoMode) {
        app.startRequested = true;
    }
}

static void
cleanupFinishedJobs(AppState& app)
{
    CompletedJob completed;
    while (repair_queue_pop_result(app.repairQueue, &completed, false)) {
        if (completed.result.total_time_ms > 0.0) {
            app.jobDurationHistoryMs.push_back(completed.result.total_time_ms);
            int window             = std::max(app.options.queueSize, 1);
            const size_t maxWindow = 64;
            size_t limit           = static_cast<size_t>(window);
            if (limit > maxWindow) {
                limit = maxWindow;
            }
            while (app.jobDurationHistoryMs.size() > limit) {
                app.jobDurationHistoryMs.pop_front();
            }
        }

        std::string inputPath;
        std::string outputPath;
        for (auto it = app.activeJobs.begin(); it != app.activeJobs.end(); ++it) {
            if (it->id == completed.job_id) {
                inputPath  = it->inputPath;
                outputPath = it->outputPath;
                app.activeJobs.erase(it);
                break;
            }
        }

        if (completed.result.status == RepairJobStatus::Ok) {
            app.completedJobs++;
            if (app.options.verbosity > 0) {
                const auto& stats = completed.result.stats;

                std::ostringstream stats_report;
                stats_report << "=== Detailed Statistics ===\n";
                stats_report << "Original mesh:\n";
                stats_report << "  Vertices: " << stats.original_vertices << "\n";
                stats_report << "  Faces: " << stats.original_faces << "\n";

                stats_report << "Final mesh:\n";
                stats_report << "  Vertices: " << stats.final_vertices << " ("
                             << "+" << MeshRepair::mesh_stats_total_vertices_added(stats) << ")\n";
                stats_report << "  Faces: " << stats.final_faces << " ("
                             << "+" << MeshRepair::mesh_stats_total_faces_added(stats) << ")\n";

                stats_report << "Hole processing:\n";
                stats_report << "  Detected: " << stats.num_holes_detected << "\n";
                stats_report << "  Filled: " << stats.num_holes_filled << "\n";
                stats_report << "  Failed: " << stats.num_holes_failed << "\n";
                stats_report << "  Skipped: " << stats.num_holes_skipped << "\n";

                stats_report << "Timing breakdown:\n";
                if (stats.detection_time_ms > 0.0) {
                    stats_report << "  Detection: " << stats.detection_time_ms << " ms\n";
                }
                if (stats.partition_time_ms > 0.0) {
                    stats_report << "  Partition: " << stats.partition_time_ms << " ms\n";
                }
                if (stats.neighborhood_time_ms > 0.0) {
                    stats_report << "  Neighborhood: " << stats.neighborhood_time_ms << " ms\n";
                }
                if (stats.extraction_time_ms > 0.0) {
                    stats_report << "  Extraction: " << stats.extraction_time_ms << " ms\n";
                }
                if (stats.fill_time_ms > 0.0) {
                    stats_report << "  Hole filling: " << stats.fill_time_ms << " ms\n";
                }
                if (stats.merge_time_ms > 0.0) {
                    stats_report << "  Merge: " << stats.merge_time_ms << " ms\n";
                }
                if (stats.cleanup_time_ms > 0.0) {
                    stats_report << "  Cleanup: " << stats.cleanup_time_ms << " ms\n";
                }
                if (stats.total_time_ms > 0.0) {
                    stats_report << "  Pipeline total: " << stats.total_time_ms << " ms\n";
                }
                if (completed.result.total_time_ms > 0.0) {
                    stats_report << "  Job total: " << completed.result.total_time_ms << " ms\n";
                }

                if (!inputPath.empty()) {
                    stats_report << "Input file: " << inputPath << "\n";
                }
                if (!outputPath.empty()) {
                    stats_report << "Output file: " << outputPath << "\n";
                }

                MeshRepair::logInfo(MeshRepair::LogCategory::Cli, stats_report.str());

                if (app.options.perHoleInfo && !stats.hole_details.empty()) {
                    std::ostringstream per_hole_report;
                    per_hole_report << "Per-hole details:\n";
                    for (size_t i = 0; i < stats.hole_details.size(); ++i) {
                        const auto& h = stats.hole_details[i];
                        per_hole_report << "  Hole " << (i + 1) << ": ";
                        if (h.filled_successfully) {
                            per_hole_report << "OK - " << h.num_faces_added << " faces, " << h.num_vertices_added
                                            << " vertices, " << h.fill_time_ms << " ms";
                            if (!h.fairing_succeeded) {
                                per_hole_report << " [fairing failed]";
                            }
                        } else {
                            per_hole_report << "FAILED";
                            if (!h.error_message.empty()) {
                                per_hole_report << " - " << h.error_message;
                            }
                        }
                        per_hole_report << "\n";
                    }
                    MeshRepair::logInfo(MeshRepair::LogCategory::Cli, per_hole_report.str());
                }

                std::string doneMsg = "Done! Successfully processed mesh";
                if (!inputPath.empty()) {
                    doneMsg += ": " + inputPath;
                }
                MeshRepair::logInfo(MeshRepair::LogCategory::Cli, doneMsg);
            }
        } else {
            app.failedJobs++;
            std::string message = "Job failed";
            const char* label   = jobStatusLabel(completed.result.status);
            if (label) {
                message = std::string("Job ") + label;
            }
            if (!completed.result.error_text.empty()) {
                message += ": " + completed.result.error_text;
            }
            if (!inputPath.empty()) {
                message += " (" + inputPath + ")";
            }
            app.statusMessage = message;
            MeshRepair::logError(MeshRepair::LogCategory::Cli, message);
        }
    }
}

static void
launchAvailableJobs(AppState& app)
{
    bool hadWork = hasActiveJobs(app) || !app.batchQueue.empty();
    if (!app.batchTimingActive && hadWork && !app.paused && !app.cancelRequested) {
        app.batchTimingActive = true;
        app.batchStartTime    = std::chrono::steady_clock::now();
    }

    ensureRepairQueue(app);
    cleanupFinishedJobs(app);

    if (app.cancelRequested) {
        for (auto& job : app.activeJobs) {
            if (job.cancel_flag) {
                job.cancel_flag->store(true, std::memory_order_relaxed);
            }
        }
    }

    bool drained = !hasActiveJobs(app) && app.batchQueue.empty();
    if (app.cancelRequested && drained) {
        if (app.batchTimingActive) {
            auto now = std::chrono::steady_clock::now();
            app.lastBatchTotalTimeMs
                += std::chrono::duration<double, std::milli>(now - app.batchStartTime).count();
            app.batchTimingActive = false;
        }

        app.startRequested  = false;
        app.paused          = false;
        app.cancelRequested = false;

        double seconds = app.lastBatchTotalTimeMs / 1000.0;
        int rounded    = static_cast<int>(std::round(seconds));
        std::ostringstream oss;
        oss << "Batch canceled";
        if (rounded > 0) {
            oss << " after " << formatSeconds(rounded);
        }
        oss << ".";
        app.statusMessage = oss.str();

        app.completedJobs = 0;
        app.failedJobs    = 0;
        return;
    }

    if (app.paused) {
        if (app.batchTimingActive && !hasActiveJobs(app)) {
            auto now = std::chrono::steady_clock::now();
            app.lastBatchTotalTimeMs
                += std::chrono::duration<double, std::milli>(now - app.batchStartTime).count();
            app.batchTimingActive = false;
        }
        return;
    }

    if (app.cancelRequested) {
        return;
    }

    int maxParallel  = std::max(app.parallelJobs, 1);
    bool canSchedule = app.autoMode || app.startRequested || hasActiveJobs(app);

    double currentMem = 0.0;
    for (const auto& job : app.activeJobs) {
        currentMem += job.sizeGb * kMemoryMultiplier;
    }

    while (canSchedule && !app.batchQueue.empty() && static_cast<int>(app.activeJobs.size()) < maxParallel) {
        const std::string path = app.batchQueue.front();
        double sizeGb          = fileSizeGb(fs::path(path));
        double projectedMem    = currentMem + sizeGb * kMemoryMultiplier;
        if (projectedMem > static_cast<double>(app.ramLimitGb)) {
            break;  // Wait for memory headroom
        }

        app.batchQueue.pop_front();
        GuiOptions opts  = app.options;
        opts.inputPath   = path;
        opts.outputPath  = resolveOutputPath(path, app.options.outputPath, app.options.outputPostfix,
                                             app.options.outputFormat);
        opts.colorOutput = false;

        RepairJobConfig cfg = buildJobConfig(opts);
        auto cancel_token   = std::make_shared<std::atomic<bool>>(false);
        cfg.cancel_token    = cancel_token;
        uint64_t jobId      = 0;
        if (!repair_queue_enqueue(app.repairQueue, cfg, &jobId)) {
            app.statusMessage = "Queue is full; waiting for slots.";
            break;
        }

        BatchJob job {};
        job.id          = jobId;
        job.inputPath   = opts.inputPath;
        job.outputPath  = opts.outputPath;
        job.sizeGb      = sizeGb;
        job.cancel_flag = cancel_token;
        app.activeJobs.push_back(job);
        currentMem += sizeGb * kMemoryMultiplier;
    }

    if (!hasActiveJobs(app) && !app.batchQueue.empty() && canSchedule) {
        app.statusMessage = "Queued files waiting for RAM headroom or parallel slots.";
    }

    bool hasWorkNow = hasActiveJobs(app) || !app.batchQueue.empty();
    if (!hadWork && hasWorkNow) {
        app.batchTimingActive    = true;
        app.jobDurationHistoryMs.clear();
        app.batchStartTime       = std::chrono::steady_clock::now();
    }

    if (!hasWorkNow) {
        if (app.batchTimingActive) {
            auto now = std::chrono::steady_clock::now();
            app.lastBatchTotalTimeMs
                += std::chrono::duration<double, std::milli>(now - app.batchStartTime).count();
            app.batchTimingActive = false;
        }

        app.startRequested  = false;
        app.cancelRequested = false;
        app.paused          = false;
        if (hadWork) {
            if (app.failedJobs > 0) {
                double seconds = app.lastBatchTotalTimeMs / 1000.0;
                int rounded    = static_cast<int>(std::round(seconds));
                std::ostringstream oss;
                oss << "Batch finished with failures";
                if (rounded > 0) {
                    oss << " in " << formatSeconds(rounded);
                }
                oss << ".";
                app.statusMessage = oss.str();
            } else if (app.completedJobs > 0) {
                double seconds = app.lastBatchTotalTimeMs / 1000.0;
                int rounded    = static_cast<int>(std::round(seconds));
                std::ostringstream oss;
                oss << "Batch finished successfully";
                if (rounded > 0) {
                    oss << " in " << formatSeconds(rounded);
                }
                oss << ".";
                app.statusMessage = oss.str();
            } else if (app.statusMessage == "Processing in progress. Drop ignored.") {
                app.statusMessage = "Ready.";
            }
        } else if (app.statusMessage == "Processing in progress. Drop ignored.") {
            app.statusMessage = "Ready.";
        }
    }
}

static void
consumePendingDrops(AppState& app)
{
    if (hasActiveJobs(app)) {
        app.statusMessage = "Processing in progress. Drop ignored.";
        return;
    }

    std::vector<std::string> drops;
    {
        std::lock_guard<std::mutex> lock(app.dropMutex);
        drops.swap(app.pendingDrops);
    }

    if (drops.empty()) {
        return;
    }

    enqueuePaths(app, drops);
}

static GLFWmonitor*
monitorForCursor()
{
    GLFWmonitor* primary   = glfwGetPrimaryMonitor();
    int monitorCount       = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    if (!monitors || monitorCount == 0) {
        return primary;
    }

    double cursorX     = 0.0;
    double cursorY     = 0.0;
    bool haveCursorPos = false;

#if defined(_WIN32)
    POINT pt {};
    if (GetCursorPos(&pt)) {
        cursorX       = static_cast<double>(pt.x);
        cursorY       = static_cast<double>(pt.y);
        haveCursorPos = true;
    }
#elif defined(GLFW_EXPOSE_NATIVE_X11)
    Display* display = glfwGetX11Display();
    if (display) {
        Window root = DefaultRootWindow(display);
        Window child;
        Window rootReturn;
        int rootX = 0, rootY = 0;
        int winX = 0, winY = 0;
        unsigned int mask = 0;
        if (XQueryPointer(display, root, &rootReturn, &child, &rootX, &rootY, &winX, &winY, &mask)) {
            cursorX       = static_cast<double>(rootX);
            cursorY       = static_cast<double>(rootY);
            haveCursorPos = true;
        }
    }
#endif

    if (!haveCursorPos) {
        return primary;
    }

    GLFWmonitor* bestMonitor   = primary;
    double bestDistanceSquared = std::numeric_limits<double>::max();
    for (int i = 0; i < monitorCount; ++i) {
        int x = 0, y = 0, w = 0, h = 0;
        glfwGetMonitorWorkarea(monitors[i], &x, &y, &w, &h);
        if (cursorX >= x && cursorX <= x + w && cursorY >= y && cursorY <= y + h) {
            return monitors[i];
        }

        double dx    = cursorX < x ? x - cursorX : (cursorX > x + w ? cursorX - (x + w) : 0.0);
        double dy    = cursorY < y ? y - cursorY : (cursorY > y + h ? cursorY - (y + h) : 0.0);
        double dist2 = dx * dx + dy * dy;
        if (dist2 < bestDistanceSquared) {
            bestDistanceSquared = dist2;
            bestMonitor         = monitors[i];
        }
    }

    return bestMonitor;
}

static void
centerWindow(GLFWwindow* window, int width, int height)
{
    GLFWmonitor* monitor = monitorForCursor();
    if (!monitor) {
        return;
    }

    int monitorX = 0;
    int monitorY = 0;
    int monitorW = 0;
    int monitorH = 0;
    glfwGetMonitorWorkarea(monitor, &monitorX, &monitorY, &monitorW, &monitorH);

    int xpos = monitorX + (monitorW - width) / 2;
    int ypos = monitorY + (monitorH - height) / 2;
    glfwSetWindowPos(window, xpos, ypos);
}

static void
applyWindowSize(GLFWwindow* window, AppState& app)
{
    app.options.windowWidth  = std::max(app.options.windowWidth, kMinWindowWidth);
    app.options.windowHeight = std::max(app.options.windowHeight, kMinWindowHeight);
    glfwSetWindowSizeLimits(window, kMinWindowWidth, kMinWindowHeight, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetWindowSize(window, app.options.windowWidth, app.options.windowHeight);
    centerWindow(window, app.options.windowWidth, app.options.windowHeight);
}

static void
setWindowAlwaysOnTop(GLFWwindow* window, bool enable)
{
    if (!window) {
        return;
    }
    glfwSetWindowAttrib(window, GLFW_FLOATING, enable ? GLFW_TRUE : GLFW_FALSE);
}

static bool
openFolderDialog(std::string& outPath, GLFWwindow* window)
{
    FloatingScope floatingGuard(window, true);
    nfdchar_t* path    = nullptr;
    nfdresult_t result = NFD_PickFolder(&path, nullptr);
    if (result == NFD_OKAY && path) {
        outPath.assign(path);
        NFD_FreePath(path);
        return true;
    }

    if (path) {
        NFD_FreePath(path);
    }

    return result == NFD_CANCEL;
}

static std::vector<std::string>
openFileDialogMultiple(GLFWwindow* window)
{
    FloatingScope floatingGuard(window, true);
    std::vector<std::string> paths;
    const nfdpathset_t* pathSet = nullptr;
    nfdresult_t result          = NFD_OpenDialogMultiple(&pathSet, nullptr, 0, nullptr);
    if (result == NFD_OKAY && pathSet) {
        nfdpathsetsize_t count = 0;
        if (NFD_PathSet_GetCount(pathSet, &count) == NFD_OKAY) {
            for (nfdpathsetsize_t i = 0; i < count; ++i) {
                nfdchar_t* path = nullptr;
                if (NFD_PathSet_GetPath(pathSet, i, &path) == NFD_OKAY && path) {
                    paths.emplace_back(path);
                    NFD_PathSet_FreePath(path);
                }
            }
        }
    }

    if (pathSet) {
        NFD_PathSet_Free(pathSet);
    }
    return paths;
}

static bool
openSaveDialog(std::string& outPath, const std::string& defaultPath, GLFWwindow* window)
{
    FloatingScope floatingGuard(window, true);
    nfdchar_t* path       = nullptr;
    const char* def_c_str = defaultPath.empty() ? nullptr : defaultPath.c_str();
    nfdresult_t result    = NFD_SaveDialog(&path, nullptr, 0, def_c_str, nullptr);
    if (result == NFD_OKAY && path) {
        outPath.assign(path);
        NFD_FreePath(path);
        return true;
    }

    if (path) {
        NFD_FreePath(path);
    }

    return result == NFD_CANCEL;
}

static void
renderMainMenu(AppState& app, GLFWwindow* window)
{
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            bool nfdEnabled = app.nfdReady;
            if (ImGui::MenuItem("Open File", nullptr, false, nfdEnabled)) {
                auto selections = openFileDialogMultiple(window);
                if (!selections.empty()) {
                    enqueuePaths(app, selections);
                } else {
                    app.statusMessage = "File open canceled or failed.";
                }
            }

            if (ImGui::MenuItem("Open Folder", nullptr, false, nfdEnabled)) {
                std::string selectedFolder;
                if (openFolderDialog(selectedFolder, window)) {
                    if (!selectedFolder.empty()) {
                        enqueuePaths(app, std::vector<std::string> { selectedFolder });
                    }
                } else {
                    app.statusMessage = "Folder open canceled or failed.";
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                glfwSetWindowShouldClose(window, GLFW_TRUE);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Options")) {
            ImGui::MenuItem("Recursive", nullptr, &app.recursiveInput, true);
            ImGui::Separator();
            auto themeItem = [&](const char* label, ThemeMode mode) {
                bool selected = app.theme == mode;
                if (ImGui::MenuItem(label, nullptr, selected)) {
                    app.theme = mode;
                    applyTheme(app);
                }
            };
            themeItem("Light", ThemeMode::MrLight);
            themeItem("Dark", ThemeMode::MrDark);
            themeItem("ImGui Light", ThemeMode::ImGuiLight);
            themeItem("ImGui Dark", ThemeMode::ImGuiDark);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

static void
renderProcessingOverlay(AppState& app)
{
    bool showOverlay = hasActiveJobs(app) || !app.batchQueue.empty();
    if (!showOverlay) {
        return;
    }

    int completedJobs = app.completedJobs + app.failedJobs;
    int activeJobs    = static_cast<int>(app.activeJobs.size());
    int pendingJobs   = static_cast<int>(app.batchQueue.size());
    int totalJobs     = completedJobs + activeJobs + pendingJobs;
    int progressed    = completedJobs + activeJobs;
    int remainingJobs = activeJobs + pendingJobs;
    float progress    = totalJobs > 0 ? static_cast<float>(progressed) / static_cast<float>(totalJobs) : 0.0f;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 overlayPos       = viewport->Pos;
    ImVec2 overlaySize      = viewport->Size;

    ImGui::SetNextWindowPos(overlayPos);
    ImGui::SetNextWindowSize(overlaySize);
    ImGui::SetNextWindowFocus();

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking
                             | ImGuiWindowFlags_NoNav;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImVec4 overlayBg(0.0f, 0.0f, 0.0f, 0.5f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, overlayBg);

    if (ImGui::Begin("ProcessingOverlay", nullptr, flags)) {
        ImVec2 winPos  = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();

        ImVec2 panelSize(winSize.x, winSize.y * 0.33f);
        float panelY = (winSize.y - panelSize.y) * 0.5f;

        ImVec2 panelMin(winPos.x, winPos.y + panelY);
        ImVec2 panelMax(panelMin.x + panelSize.x, panelMin.y + panelSize.y);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImU32 panelBgColor   = app.darkTheme ? IM_COL32(0, 0, 0, 180) : IM_COL32(255, 255, 255, 180);
        float panelRounding  = 0.0f;
        drawList->AddRectFilled(panelMin, panelMax, panelBgColor, panelRounding);

        ImGuiStyle& style = ImGui::GetStyle();

        ImVec2 panelLocalMin(panelMin.x - winPos.x, panelMin.y - winPos.y);
        ImGui::SetCursorPos(panelLocalMin);

        if (ImGui::BeginChild("ProcessingPanel", panelSize, false,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
            float innerWidth = panelSize.x;
            float buttonW    = 90.0f;
            float barHeight  = ImGui::GetFrameHeight();
            float textHeight = ImGui::GetTextLineHeight();

            // Vertical centering of the three rows (progress text, bar+buttons, ETA)
            float blockHeight = textHeight + style.ItemSpacing.y * 2.0f + barHeight;
            float topPadding  = std::max(0.0f, (panelSize.y - blockHeight) * 0.5f - textHeight);

            ImGui::Dummy(ImVec2(0.0f, topPadding));

            // Progress text centered
            std::string progressText = "Processing " + std::to_string(progressed) + "/" + std::to_string(totalJobs);
            ImVec2 progressSize      = ImGui::CalcTextSize(progressText.c_str());
            float progressX          = (innerWidth - progressSize.x) * 0.5f;
            ImGui::SetCursorPosX(progressX);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(progressText.c_str());

            // Progress bar + buttons row, horizontally centered as a block
            //ImGui::Dummy(ImVec2(0.0f, style.ItemSpacing.y));

            float barWidth       = std::max(100.0f, innerWidth * 0.6f);
            float totalButtonsW  = buttonW * 2.0f + style.ItemSpacing.x;
            float rowWidth       = barWidth + style.ItemSpacing.x + totalButtonsW;
            float rowStartX      = std::max(0.0f, (innerWidth - rowWidth) * 0.5f);
            ImGui::SetCursorPosX(rowStartX);

            ImGui::ProgressBar(progress, ImVec2(barWidth, barHeight));
            ImGui::SameLine(0.0f, style.ItemSpacing.x);
            if (ImGui::Button(app.paused ? "Resume" : "Pause", ImVec2(buttonW, 0))) {
                app.paused        = !app.paused;
                app.statusMessage = app.paused ? "Batch paused." : "Batch resumed.";
                if (!app.paused) {
                    app.startRequested = true;
                }
            }
            ImGui::SameLine(0.0f, style.ItemSpacing.x);
            if (ImGui::Button("Cancel", ImVec2(buttonW, 0))) {
                app.cancelRequested = true;
                app.batchQueue.clear();
                app.startRequested = false;
                app.paused         = false;
                for (auto& job : app.activeJobs) {
                    if (job.cancel_flag) {
                        job.cancel_flag->store(true, std::memory_order_relaxed);
                    }
                }
                app.statusMessage = "Batch cancel requested. Waiting for active jobs to finish.";
            }

            // Elapsed / ETA line, centered
            //ImGui::Dummy(ImVec2(0.0f, style.ItemSpacing.y));

            double elapsedMs = app.lastBatchTotalTimeMs;
            if (app.batchTimingActive) {
                auto now = std::chrono::steady_clock::now();
                elapsedMs += std::chrono::duration<double, std::milli>(now - app.batchStartTime).count();
            }

            double avgMs = 0.0;
            if (!app.jobDurationHistoryMs.empty()) {
                double sum = 0.0;
                for (double v : app.jobDurationHistoryMs) {
                    sum += v;
                }
                avgMs = sum / static_cast<double>(app.jobDurationHistoryMs.size());
            }

            double etaMs = 0.0;
            if (avgMs > 0.0 && remainingJobs > 0 && completedJobs > 0) {
                size_t workers = app.queueWorkers > 0 ? app.queueWorkers : 1;
                etaMs          = avgMs * (static_cast<double>(remainingJobs) / static_cast<double>(workers));
            }

            if (elapsedMs > 0.0 || etaMs > 0.0) {
                int elapsedSec = elapsedMs > 0.0 ? static_cast<int>(std::round(elapsedMs / 1000.0)) : 0;
                int etaSec     = etaMs > 0.0 ? static_cast<int>(std::round(etaMs / 1000.0)) : 0;

                std::string elapsedStr = formatSeconds(elapsedSec);
                std::string label;
                if (etaSec > 0 && remainingJobs > 0) {
                    std::string etaStr = formatSeconds(etaSec);
                    label              = "Elapsed: " + elapsedStr + " | ETA: " + etaStr;
                } else {
                    label = "Elapsed: " + elapsedStr;
                }

                ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
                float labelX     = (innerWidth - labelSize.x) * 0.5f;
                ImGui::SetCursorPosX(labelX);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(label.c_str());
            }
        }
        ImGui::EndChild();
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

static void
renderDragOverlay(AppState& app)
{
    if (!app.dragOverlayActive) {
        return;
    }

    if (hasActiveJobs(app) || !app.batchQueue.empty()) {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 pos              = viewport->Pos;
    ImVec2 size             = viewport->Size;

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove
                             | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking
                             | ImGuiWindowFlags_NoNav;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    float alpha           = app.darkTheme ? 0.5f : 0.5f;
    ImVec4 overlayBgColor = ImVec4(0.0f, 0.0f, 0.0f, alpha);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, overlayBgColor);

    if (ImGui::Begin("DragDropOverlay", nullptr, flags)) {
        const char* label = "Drag and Drop files to process";
        ImVec2 winSize    = ImGui::GetWindowSize();
        ImVec2 textSize   = ImGui::CalcTextSize(label);
        ImVec2 cursorPos  = ImVec2((winSize.x - textSize.x) * 0.5f, (winSize.y - textSize.y) * 0.5f);
        ImGui::SetCursorPos(cursorPos);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0, 1.0f, 1.0f, 1.0f));
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

static void
onDndDragEnter(GLFWwindow* window, const dnd_glfw::DragEvent& event, void* userData)
{
    (void)window;
    auto* app = static_cast<AppState*>(userData);
    if (!app) {
        return;
    }
    if (hasActiveJobs(*app) || !app->batchQueue.empty()) {
        return;
    }
    if (event.kind == dnd_glfw::PayloadKind::Files) {
        app->dragOverlayActive = true;
    }
}

static void
onDndDragOver(GLFWwindow* window, const dnd_glfw::DragEvent& event, void* userData)
{
    (void)window;
    (void)event;
    (void)userData;
}

static void
onDndDragLeave(GLFWwindow* window, void* userData)
{
    (void)window;
    auto* app = static_cast<AppState*>(userData);
    if (!app) {
        return;
    }
    app->dragOverlayActive = false;
}

static void
onDndDrop(GLFWwindow* window, const dnd_glfw::DropEvent& event, void* userData)
{
    (void)window;
    auto* app = static_cast<AppState*>(userData);
    if (!app) {
        return;
    }
    if (hasActiveJobs(*app)) {
        app->statusMessage     = "Processing in progress. Drop ignored.";
        app->dragOverlayActive = false;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(app->dropMutex);
        app->pendingDrops = event.paths;
    }

    app->dragOverlayActive = false;
}

static void
onDndDragCancel(GLFWwindow* window, void* userData)
{
    (void)window;
    auto* app = static_cast<AppState*>(userData);
    if (!app) {
        return;
    }
    app->dragOverlayActive = false;
}

int
main(int argc, char** argv)
{
    (void)argc;

    AppState app {};
    std::string configPath = configFilePath();
    loadAppConfig(app, configPath);

    MeshRepair::LoggerConfig log_cfg;
    log_cfg.useStderr = true;
    log_cfg.useColors = app.options.colorOutput;
    log_cfg.minLevel  = MeshRepair::logLevelFromVerbosity(app.options.verbosity);
    MeshRepair::initLogger(log_cfg);

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        fprintf(stderr, "Failed to initialize GLFW\n");
        return 1;
    }

    int windowWidth  = std::max(app.options.windowWidth, kMinWindowWidth);
    int windowHeight = std::max(app.options.windowHeight, kMinWindowHeight);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

    GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, "MeshRepair GUI", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        fprintf(stderr, "Failed to create GLFW window\n");
        return 1;
    }

    glfwSetWindowSizeLimits(window, kMinWindowWidth, kMinWindowHeight, GLFW_DONT_CARE, GLFW_DONT_CARE);

    glfwSetWindowUserPointer(window, &app);

    dnd_glfw::Callbacks dndCallbacks {};
    dndCallbacks.dragEnter  = &onDndDragEnter;
    dndCallbacks.dragOver   = &onDndDragOver;
    dndCallbacks.dragLeave  = &onDndDragLeave;
    dndCallbacks.drop       = &onDndDrop;
    dndCallbacks.dragCancel = &onDndDragCancel;

    dnd_glfw::init(window, dndCallbacks, &app);

    glfwMakeContextCurrent(window);
    glewExperimental  = GL_TRUE;
    GLenum glewStatus = glewInit();
    if (glewStatus != GLEW_OK) {
        fprintf(stderr, "GLEW init failed: %s\n", reinterpret_cast<const char*>(glewGetErrorString(glewStatus)));
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    glfwSwapInterval(1);

    const char* glslVersion = "#version 330";

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    if (!app.imguiIniData.empty()) {
        ImGui::LoadIniSettingsFromMemory(app.imguiIniData.c_str(), app.imguiIniData.size());
    }
    applyTheme(app);

    {  // Customize style
        ImGuiStyle& style             = ImGui::GetStyle();
        style.WindowBorderSize        = 0.0f;  // affects all windows
        style.PopupBorderSize         = 0.0f;
        style.SeparatorTextAlign      = ImVec2(0.5f, 0.5f);
        style.SeparatorTextBorderSize = 2.0f;
    }

    applyWindowSize(window, app);
    setWindowAlwaysOnTop(window, true);
    glfwShowWindow(window);

    const ImWchar* glyphRanges = io.Fonts->GetGlyphRangesDefault();
    std::string fontPath       = findFontPath(argv ? argv[0] : nullptr);
    if (!fontPath.empty()) {
        ImFont* font = io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 16.0f, nullptr, glyphRanges);
        if (font) {
            io.FontDefault = font;
        } else {
            fprintf(stderr, "Failed to load font at %s, using default ImGui font.\n", fontPath.c_str());
        }
    } else {
        fprintf(stderr, "No FiraSans font found; using default ImGui font.\n");
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glslVersion);

    nfdresult_t nfdInitResult = NFD_Init();
    if (nfdInitResult == NFD_OKAY) {
        app.nfdReady      = true;
        app.statusMessage = "Ready. Drag a mesh or use File -> Open File.";
    } else {
        const char* err   = NFD_GetError();
        app.nfdReady      = false;
        app.statusMessage = std::string("Native file dialog init failed: ") + (err ? err : "unknown error");
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        consumePendingDrops(app);
        launchAvailableJobs(app);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            app.paused        = !app.paused;
            app.statusMessage = app.paused ? "Batch paused. Press Ctrl+C or Resume to continue." : "Batch resumed.";
            if (!app.paused) {
                app.startRequested = true;
            }
        }

        bool processingOverlayActive = hasActiveJobs(app) || !app.batchQueue.empty();
        bool uiLocked                = processingOverlayActive;
        {
            DisableGuard disableMenu(uiLocked);
            renderMainMenu(app, window);
        }

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove
                                       | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;

        if (ImGui::Begin("MeshRepair", nullptr, windowFlags)) {
            DisableGuard lockUi(uiLocked);
            ImGui::SetCursorPosY(ImGui::GetFrameHeight());

            if (ImGui::BeginTable("layout", 1, ImGuiTableFlags_SizingStretchProp)) {
                // Inputs / Outputs
                ImVec2 buttonSize = { 80.0f, ImGui::GetFrameHeight() };
                float space       = buttonSize.x + ImGui::GetStyle().ItemSpacing.x * 2.0f;
                float fistTab     = 100.0f;

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::BeginTable("io_block", 1, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();

                    ImGui::Spacing();
                    ImGui::Spacing();
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Input file");

                    float inputWidth = ImGui::GetContentRegionAvail().x - 2.0f * buttonSize.x
                                       - ImGui::GetStyle().ItemSpacing.x - fistTab;
                    float buttonsTab = inputWidth + buttonSize.x * 2.0f + ImGui::GetStyle().ItemSpacing.x * 4.5f;

                    {
                        DisableGuard disableInputs(app.autoMode);
                        ImGui::SameLine(fistTab);
                        ImGui::SetNextItemWidth(inputWidth);
                        ImGui::InputText("##input-path", &app.options.inputPath);
                    }

                    ImGui::SameLine();
                    if (ImGui::Checkbox("Auto##auto-mode", &app.autoMode)) {
                        app.statusMessage = app.autoMode ? "Automatic mode enabled." : "Automatic mode disabled.";
                    }

                    {
                        DisableGuard disableInputs(app.autoMode);
                        ImGui::SameLine(buttonsTab);
                        if (ImGui::Button("Open…##input", buttonSize) && app.nfdReady) {
                            auto selections = openFileDialogMultiple(window);
                            if (!selections.empty()) {
                                app.options.inputPath = selections.front();
                                enqueuePaths(app, selections);
                            }
                        }
                    }
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Output path");
                    ImGui::SameLine(fistTab);
                    ImGui::SetNextItemWidth(inputWidth);
                    ImGui::InputText("##output-path", &app.options.outputPath);
                    ImGui::SameLine(buttonsTab);
                    if (ImGui::Button(app.autoMode ? "Path…##output" : "Save…##output", buttonSize) && app.nfdReady) {
                        if (app.autoMode) {
                            std::string selectedFolder;
                            if (openFolderDialog(selectedFolder, window) && !selectedFolder.empty()) {
                                app.options.outputPath = selectedFolder;
                                app.statusMessage      = "Selected output folder.";
                            }
                        } else {
                            std::string suggested = app.options.outputPath;
                            if (suggested.empty() && !app.options.inputPath.empty()) {
                                suggested = resolveOutputPath(app.options.inputPath, app.options.outputPath,
                                                              app.options.outputPostfix, app.options.outputFormat);
                            }
                            std::string selectedOut;
                            if (openSaveDialog(selectedOut, suggested, window) && !selectedOut.empty()) {
                                app.options.outputPath = selectedOut;
                                app.statusMessage      = "Selected output file.";
                            }
                        }
                    }
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Postfix");
                    ImGui::SameLine(fistTab);
                    ImGui::SetNextItemWidth(inputWidth);
                    ImGui::InputText("##output-postfix", &app.options.outputPostfix);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(buttonSize.x);
                    const char* fmtItems[] = { "OBJ", "PLY", "OFF" };
                    ImGui::Combo("##output-format", &app.options.outputFormat, fmtItems, IM_ARRAYSIZE(fmtItems));
                    ImGui::SameLine();
                    if (ImGui::Button("Default##postfix", buttonSize)) {
                        app.options.outputPostfix.clear();
                        app.options.outputPostfix = "_repaired";
                        app.options.outputFormat  = 1;  // PLY
                        if (!app.options.inputPath.empty()) {
                            app.options.outputPath = resolveOutputPath(app.options.inputPath, app.options.outputPath,
                                                                       app.options.outputPostfix,
                                                                       app.options.outputFormat);
                        }
                    }
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Temp folder");
                    ImGui::SameLine(fistTab);
                    ImGui::SetNextItemWidth(inputWidth);
                    ImGui::InputText("##temp-dir", &app.options.tempDir);
                    ImGui::SameLine(buttonsTab);
                    if (ImGui::Button("Select##temp", buttonSize) && app.nfdReady) {
                        std::string selectedTemp;
                        if (openFolderDialog(selectedTemp, window) && !selectedTemp.empty()) {
                            app.options.tempDir = selectedTemp;
                            app.statusMessage   = "Selected temp directory.";
                        }
                    }
                    ImGui::EndTable();
                }

                // Preprocessing | Hole Filling
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::BeginTable("proc_fill", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::SeparatorText("Preprocessing");
                    ImGui::Checkbox("Enable preprocessing", &app.options.enablePreprocessing);
                    ImGui::Checkbox("Remove duplicates", &app.options.preprocessRemoveDuplicates);
                    ImGui::Checkbox("Remove non-manifold", &app.options.preprocessRemoveNonManifold);
                    ImGui::Checkbox("Remove 3-face fans", &app.options.preprocessRemove3FaceFans);
                    ImGui::Checkbox("Remove isolated vertices", &app.options.preprocessRemoveIsolated);
                    ImGui::Checkbox("Keep largest component", &app.options.preprocessKeepLargest);
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Non-manifold passes");
                    ImGui::InputInt("##non-manifold-passes", &app.options.nonManifoldPasses);
                    ImGui::Checkbox("Holes only (partitioned mode)", &app.options.holesOnly);

                    ImGui::TableNextColumn();
                    ImGui::SeparatorText("Hole Filling");
                    ImGui::SliderInt("Continuity", &app.options.continuity, 0, 2);
                    ImGui::InputInt("Max boundary vertices", &app.options.maxBoundary);
                    ImGui::InputDouble("Max diameter ratio", &app.options.maxDiameterRatio, 0.01, 0.05, "%.3f");
                    ImGui::Checkbox("Remove long-edge polygons", &app.options.preprocessRemoveLongEdges);
                    {
                        DisableGuard guard(!app.options.preprocessRemoveLongEdges);
                        if (ImGui::InputDouble("Max edge ratio", &app.options.preprocessMaxEdgeRatio, 0.001, 0.01,
                                               "%.4f")) {
                            if (app.options.preprocessMaxEdgeRatio < 0.0) {
                                app.options.preprocessMaxEdgeRatio = 0.0;
                            }
                            if (app.options.preprocessMaxEdgeRatio > 1.0) {
                                app.options.preprocessMaxEdgeRatio = 1.0;
                            }
                        }
                    }
                    ImGui::Checkbox("Use 2D CDT", &app.options.use2dCdt);
                    ImGui::Checkbox("Use 3D Delaunay", &app.options.use3dDelaunay);
                    ImGui::Checkbox("Skip cubic search", &app.options.skipCubicSearch);
                    ImGui::Checkbox("Refine patch", &app.options.refine);
                    ImGui::EndTable();
                }

                // Run button full width
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
                ImVec2 runSize(-FLT_MIN, ImGui::GetFrameHeight() * 3.0f);
                bool busy = hasActiveJobs(app);
                if (ImGui::Button(busy ? "Processing..." : "Run", runSize)) {
                    if (!busy) {
                        if (!app.options.inputPath.empty()) {
                            fs::path inputPath(app.options.inputPath);
                            if (!fs::exists(inputPath) || fs::is_directory(inputPath)) {
                                app.statusMessage = "Input path must be an existing file.";
                            } else if (!isSupportedMeshFile(inputPath)) {
                                app.statusMessage = "Unsupported file format. Use OBJ/PLY/OFF.";
                            } else {
                                enqueuePaths(app, std::vector<std::string> { app.options.inputPath });
                            }
                        } else if (app.batchQueue.empty()) {
                            app.statusMessage = "Add files via Open or drag & drop.";
                        }
                        app.startRequested = true;
                        launchAvailableJobs(app);
                    }
                }
                ImGui::PopStyleVar();

                // Settings / Verbosity | Threading / Paths
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (ImGui::BeginTable("settings_threading", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::SeparatorText("Settings / Verbosity");
                    static const char* verbosityLabels[] = { "0 - Quiet", "1 - Info (stats)", "2 - Verbose",
                                                             "3 - Debug", "4 - Trace (PLY dumps)" };
                    if (ImGui::Combo("Verbosity", &app.options.verbosity, verbosityLabels,
                                     IM_ARRAYSIZE(verbosityLabels))) {
                        if (app.options.verbosity < 0) {
                            app.options.verbosity = 0;
                        } else if (app.options.verbosity > 4) {
                            app.options.verbosity = 4;
                        }
                        MeshRepair::setLogLevel(MeshRepair::logLevelFromVerbosity(app.options.verbosity));
                    }
                    ImGui::Checkbox("Validate mesh", &app.options.validate);
                    ImGui::Checkbox("ASCII PLY output", &app.options.asciiPly);
                    ImGui::Checkbox("Per-hole info", &app.options.perHoleInfo);
                    ImGui::Checkbox("Force CGAL OBJ loader", &app.options.forceCgalLoader);
                    ImGui::InputInt("Job timeout (s, 0=none)", &app.options.timeoutSeconds);

                    ImGui::TableNextColumn();
                    ImGui::SeparatorText("Threading / Paths");
                    ImGui::InputInt("Parallel jobs", &app.parallelJobs);
                    ImGui::InputInt("RAM budget (GB)", &app.ramLimitGb);
                    ImGui::Checkbox("Partitioned pipeline", &app.options.usePartitioned);
                    ImGui::InputInt("Min boundary edges", &app.options.minPartitionEdges);
                    ImGui::InputInt("Threads (0=auto)", &app.options.numThreads);
                    ImGui::InputInt("Queue size", &app.options.queueSize);
                    ImGui::EndTable();
                }
                ImGui::EndTable();
            }

            ImGui::Separator();
            ImGui::Spacing();
            if (!app.statusMessage.empty()) {
                ImGui::TextWrapped("%s", app.statusMessage.c_str());
            }
            int activeJobs = static_cast<int>(app.activeJobs.size());
            int pending    = static_cast<int>(app.batchQueue.size());
            int totalJobs  = app.completedJobs + app.failedJobs + activeJobs + pending;
            if (totalJobs > 0) {
                ImGui::Text("Queue: %d total | %d active | %d pending | %d done | %d failed%s", totalJobs, activeJobs,
                            pending, app.completedJobs, app.failedJobs, app.paused ? " (paused)" : "");
            }

            app.options.maxBoundary       = std::max(app.options.maxBoundary, 1);
            app.options.nonManifoldPasses = std::max(app.options.nonManifoldPasses, 1);
            app.options.minPartitionEdges = std::max(app.options.minPartitionEdges, 1);
            app.options.queueSize         = std::max(app.options.queueSize, 1);
            app.options.numThreads        = std::max(app.options.numThreads, 0);
            app.options.maxDiameterRatio  = std::max(app.options.maxDiameterRatio, 0.0);
            app.options.timeoutSeconds    = std::max(app.options.timeoutSeconds, 0);
            app.parallelJobs              = std::max(app.parallelJobs, 1);
            app.ramLimitGb                = std::max(app.ramLimitGb, 1);
            if (app.options.outputPostfix.empty() && app.options.outputPath.empty()) {
                app.options.outputPostfix = "_repaired";
            }
        }
        ImGui::End();

        renderDragOverlay(app);
        renderProcessingOverlay(app);

        ImGui::Render();
        int displayW = 0;
        int displayH = 0;
        glfwGetFramebufferSize(window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    app.startRequested = false;
    cleanupFinishedJobs(app);
    app.batchQueue.clear();
    for (auto& job : app.activeJobs) {
        if (job.cancel_flag) {
            job.cancel_flag->store(true, std::memory_order_relaxed);
        }
    }
    if (app.queueStarted) {
        repair_queue_shutdown(app.repairQueue);
        app.queueStarted = false;
    }

    int savedW = 0;
    int savedH = 0;
    glfwGetWindowSize(window, &savedW, &savedH);
    app.options.windowWidth  = savedW;
    app.options.windowHeight = savedH;
    saveAppConfig(app, ImGui::GetIO(), configPath);

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (app.nfdReady) {
        NFD_Quit();
    }

    dnd_glfw::shutdown(window);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
