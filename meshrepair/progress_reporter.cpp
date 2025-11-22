#include "progress_reporter.h"
#include <iomanip>

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

    std::cerr << "\n[" << operation_name_ << "] Starting...\n";
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

    std::cerr << "[" << operation_name_ << "] Completed in " << std::fixed << std::setprecision(2) << total_time
              << " seconds\n\n";
}

void
ProgressReporter::report(const std::string& message)
{
    if (!enabled_)
        return;

    std::cerr << "[" << operation_name_ << "] " << message << "\n";
}

void
ProgressReporter::print_progress_bar(size_t current, size_t total, double elapsed_seconds)
{
    const int bar_width = 50;
    float progress      = static_cast<float>(current) / total;
    int pos             = static_cast<int>(bar_width * progress);

    std::cerr << "[";
    for (int i = 0; i < bar_width; ++i) {
        if (i < pos)
            std::cerr << "=";
        else if (i == pos)
            std::cerr << ">";
        else
            std::cerr << " ";
    }

    std::cerr << "] " << int(progress * 100.0) << "% "
              << "(" << current << "/" << total << ") " << std::fixed << std::setprecision(1) << elapsed_seconds
              << "s\r";
    std::cerr.flush();

    if (current == total) {
        std::cerr << "\n";
    }
}

}  // namespace MeshRepair
