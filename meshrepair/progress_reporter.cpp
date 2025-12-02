#include "progress_reporter.h"
#include "include/logger.h"
#include <iomanip>
#include <sstream>

namespace MeshRepair {

void
ProgressReporter::start(size_t total_steps, const std::string& operation_name)
{
    if (!enabled_)
        return;

    total_steps_              = total_steps;
    operation_name_           = operation_name;
    last_reported_percentage_ = 0;
    start_time_               = std::chrono::high_resolution_clock::now();

    logInfo(LogCategory::Progress, "[" + operation_name_ + "] Starting...");
}

void
ProgressReporter::update(size_t current_step)
{
    if (!enabled_ || total_steps_ == 0)
        return;

    size_t percentage = (current_step * 100) / total_steps_;

    // Only report every 10% or on completion
    if (percentage >= last_reported_percentage_ + 10 || current_step == total_steps_) {
        last_reported_percentage_ = percentage;

        auto now       = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(now - start_time_).count();

        print_progress_bar(current_step, total_steps_, elapsed);
    }
}

void
ProgressReporter::finish()
{
    if (!enabled_)
        return;

    auto end_time     = std::chrono::high_resolution_clock::now();
    double total_time = std::chrono::duration<double>(end_time - start_time_).count();

    std::ostringstream oss;
    oss << "[" << operation_name_ << "] Completed in " << std::fixed << std::setprecision(2) << total_time
        << " seconds";
    logInfo(LogCategory::Progress, oss.str());
}

void
ProgressReporter::report(const std::string& message)
{
    if (!enabled_)
        return;

    logInfo(LogCategory::Progress, "[" + operation_name_ + "] " + message);
}

void
ProgressReporter::print_progress_bar(size_t current, size_t total, double elapsed_seconds)
{
    float progress = static_cast<float>(current) / total;

    std::ostringstream progress_msg;
    progress_msg << "[" << operation_name_ << "] " << int(progress * 100.0f) << "% (" << current << "/" << total << ") "
                 << std::fixed << std::setprecision(1) << elapsed_seconds << "s";

    logInfo(LogCategory::Progress, progress_msg.str());
}

}  // namespace MeshRepair
