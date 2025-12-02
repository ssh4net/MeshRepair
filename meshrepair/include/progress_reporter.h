#pragma once

#include <string>
#include <chrono>

namespace MeshRepair {

/**
 * @brief Simple progress reporter for long-running operations
 */
class ProgressReporter {
public:
    /**
     * @brief Start a new operation
     * @param total_steps Total number of steps in operation
     * @param operation_name Name of operation for display
     */
    void start(size_t total_steps, const std::string& operation_name);

    /**
     * @brief Update progress
     * @param current_step Current step number (0-based)
     */
    void update(size_t current_step);

    /**
     * @brief Complete the operation
     */
    void finish();

    /**
     * @brief Report a message without affecting progress
     * @param message Message to display
     */
    void report(const std::string& message);

    /**
     * @brief Enable/disable progress reporting
     * @param enabled true to enable
     */
    void set_enabled(bool enabled) { enabled_ = enabled; }

    /**
     * @brief Check if progress reporting is enabled
     * @return true if enabled
     */
    bool is_enabled() const { return enabled_; }

private:
    bool enabled_       = true;
    size_t total_steps_ = 0;
    std::string operation_name_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time_;
    size_t last_reported_percentage_ = 0;

    void print_progress_bar(size_t current, size_t total, double elapsed_seconds);
};

}  // namespace MeshRepair
