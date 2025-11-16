/*
 * Thread Pool Implementation for MeshRepair
 *
 * Adapted from UnRAWer threadpool.h (LGPL-3.0)
 * and SnapDiver thread_utils.h concepts
 *
 * This implementation is NOT yet integrated into the project.
 * It's prepared for future parallelization of read-only mesh operations.
 */

#pragma once

#include <queue>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <functional>
#include <future>
#include <stdexcept>
#include <atomic>
#include <iostream>

namespace MeshRepair {

// ============================================================================
// Simple Thread-Safe Queue
// ============================================================================
// Used for simple producer-consumer patterns
template<typename T> class ThreadSafeQueue {
private:
    std::queue<T> queue_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    bool finished_ = false;

public:
    // Push item to queue
    void push(T item)
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            queue_.push(std::move(item));
        }
        cv_.notify_one();
    }

    // Pop item from queue, returns false if finished and empty
    bool pop(T& item)
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_.wait(lock, [this] { return !queue_.empty() || finished_; });

        if (queue_.empty() && finished_) {
            return false;
        }

        if (!queue_.empty()) {
            item = std::move(queue_.front());
            queue_.pop();
            return true;
        }

        return false;
    }

    // Mark queue as finished (no more items will be pushed)
    void finish()
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            finished_ = true;
        }
        cv_.notify_all();
    }

    // Get current queue size
    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.size();
    }

    // Check if queue is empty
    bool empty() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.empty();
    }
};

// ============================================================================
// Memory-Bounded Queue (from SnapDiver)
// ============================================================================
// Useful for large mesh data structures - bounds by memory not count
template<typename T> class BoundedQueue {
private:
    std::queue<T> queue_;
    mutable std::mutex mtx_;
    std::condition_variable cv_push_;
    std::condition_variable cv_pop_;
    size_t max_memory_bytes_;
    size_t current_memory_bytes_ = 0;
    bool finished_               = false;

    // Helper to detect if type has memory_usage() method
    template<typename U> static auto get_memory_usage_impl(const U& item, int) -> decltype(item.memory_usage())
    {
        return item.memory_usage();
    }

    template<typename U> static size_t get_memory_usage_impl(const U& item, long)
    {
        return sizeof(item);  // Fallback
    }

    template<typename U = T> static size_t get_memory_usage(const U& item) { return get_memory_usage_impl(item, 0); }

public:
    explicit BoundedQueue(size_t max_memory_bytes)
        : max_memory_bytes_(max_memory_bytes)
    {
    }

    // Push item, blocks if memory limit reached
    void push(T item)
    {
        size_t item_memory = get_memory_usage(item);
        std::unique_lock<std::mutex> lock(mtx_);
        cv_push_.wait(lock, [this, item_memory] {
            return (current_memory_bytes_ + item_memory <= max_memory_bytes_) || finished_;
        });

        if (finished_)
            return;

        current_memory_bytes_ += item_memory;
        queue_.push(std::move(item));
        cv_pop_.notify_one();
    }

    // Pop item, returns false if finished
    bool pop(T& item)
    {
        try {
            std::unique_lock<std::mutex> lock(mtx_);

            // Wait with timeout to avoid infinite hang on Windows
            while (!finished_ && queue_.empty()) {
                auto result = cv_pop_.wait_for(lock, std::chrono::milliseconds(100));

                // After wait (timeout or notification), recheck the actual state
                // Don't continue if finished, even on timeout
                if (finished_) {
                    break;
                }

                // If not finished and queue is still empty, continue waiting
            }

            if (queue_.empty() && finished_) {
                return false;
            }

            if (!queue_.empty()) {
                item = std::move(queue_.front());
                queue_.pop();
                size_t item_memory = get_memory_usage(item);
                current_memory_bytes_ -= item_memory;
                cv_push_.notify_one();
                return true;
            }

            return false;
        } catch (const std::exception& e) {
            // Exception in pop - return false to exit thread
            return false;
        } catch (...) {
            return false;
        }
    }

    // Mark as finished
    void finish()
    {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            finished_ = true;
        }
        cv_push_.notify_all();
        cv_pop_.notify_all();
    }

    // Get statistics
    size_t size() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.size();
    }

    size_t current_memory() const
    {
        std::lock_guard<std::mutex> lock(mtx_);
        return current_memory_bytes_;
    }

    size_t max_memory() const { return max_memory_bytes_; }
};

// ============================================================================
// Full-Featured Thread Pool (from UnRAWer, enhanced)
// ============================================================================
class ThreadPool {
public:
    // Constructor: Initialize with number of threads and max queue size
    ThreadPool(size_t threads, size_t maxQueueSize = 1000)
        : stop_(false)
        , working_(0)
        , tasks_count_(0)
        , maxQueueSize_(maxQueueSize)
    {
        // Create worker threads
        for (size_t i = 0; i < threads; ++i) {
            workers_.emplace_back([this] { worker_function(); });
        }
    }

    // Deleted copy/move constructors
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&)                 = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

    // Enqueue a task with arguments, returns std::future for result
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>
    {
        using return_type = std::invoke_result_t<F, Args...>;

        // Create packaged_task with bound function and arguments
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [f = std::forward<F>(f), args = std::make_tuple(std::forward<Args>(args)...)]() mutable -> return_type {
                return std::apply(f, std::move(args));
            });

        std::future<return_type> result = task->get_future();

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            if (stop_) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }

            // Wait until there's space in queue or pool is stopped
            condition_.wait(lock, [this] { return tasks_count_ < maxQueueSize_ || stop_; });

            if (stop_) {
                throw std::runtime_error("enqueue on stopped ThreadPool");
            }

            // Add task to queue
            tasks_.emplace([task]() { (*task)(); });
            ++tasks_count_;
        }

        // Notify one worker thread
        condition_.notify_one();
        return result;
    }

    // Check if pool is idle (no tasks and no active workers)
    bool isIdle()
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        return tasks_.empty() && (working_ == 0);
    }

    // Wait for all tasks to complete
    void waitForAllTasks()
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        done_condition_.wait(lock, [this] { return tasks_count_ == 0; });
    }

    // Dynamically adjust max queue size
    void setMaxQueueSize(size_t limit)
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            maxQueueSize_ = limit;
        }
        condition_.notify_all();
    }

    // Get current queue size
    size_t queueSize() const
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        return tasks_count_;
    }

    // Get number of active workers
    size_t activeWorkers() const
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        return working_;
    }

    // Get total number of worker threads
    size_t threadCount() const { return workers_.size(); }

    // Resize thread pool dynamically
    void resize(size_t new_thread_count)
    {
        if (new_thread_count == workers_.size()) {
            return;  // No change needed
        }

        if (new_thread_count < workers_.size()) {
            // Shrink: stop excess threads
            size_t to_remove = workers_.size() - new_thread_count;

            // Signal threads to stop
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                stop_ = true;
            }
            condition_.notify_all();

            // Join and remove threads
            for (size_t i = 0; i < to_remove; ++i) {
                if (workers_.back().joinable()) {
                    workers_.back().join();
                }
                workers_.pop_back();
            }

            // Reset stop flag if threads remain
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                stop_ = (workers_.empty());
            }
        } else {
            // Grow: add new threads
            size_t to_add = new_thread_count - workers_.size();

            for (size_t i = 0; i < to_add; ++i) {
                workers_.emplace_back([this] { worker_function(); });
            }
        }
    }

    // Destructor: Stop and join all threads
    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();

        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

private:
    std::vector<std::thread> workers_;         // Worker threads
    std::queue<std::function<void()>> tasks_;  // Task queue

    mutable std::mutex queue_mutex_;          // Protects task queue
    std::condition_variable condition_;       // Task availability + queue space
    std::condition_variable done_condition_;  // Task completion

    bool stop_;            // Stop flag
    size_t working_;       // Active worker count
    size_t tasks_count_;   // Tasks in queue
    size_t maxQueueSize_;  // Max queue size

    // Worker thread function
    void worker_function()
    {
        while (true) {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(queue_mutex_);

                // Wait for task or stop signal
                condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

                if (stop_ && tasks_.empty()) {
                    return;  // Exit thread
                }

                // Get next task
                task = std::move(tasks_.front());
                tasks_.pop();
                ++working_;
            }

            // Execute task with exception safety
            try {
                task();
            } catch (const std::exception& e) {
                (void)e;  // Unused - silently ignored
                // Silently ignore exceptions - they're already handled in the task
                // Adding output here causes Windows console deadlock
            } catch (...) {
                // Silently ignore unknown exceptions
            }

            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                --working_;
                --tasks_count_;
                done_condition_.notify_all();
                condition_.notify_one();  // Notify waiting enqueuers
            }
        }
    }
};

// ============================================================================
// Helper: Get hardware core count
// ============================================================================
inline size_t
get_hardware_cores()
{
    unsigned int hw = std::thread::hardware_concurrency();
    return (hw > 0) ? hw : 4;  // Fallback to 4 if detection fails
}

// ============================================================================
// Helper: Get default thread count (half of hardware cores)
// ============================================================================
inline size_t
get_default_thread_count()
{
    size_t hw_cores = get_hardware_cores();
    return std::max<size_t>(1, hw_cores / 2);  // Half of hardware cores, minimum 1
}

// ============================================================================
// Thread-Safe Atomic Counter (for progress tracking)
// ============================================================================
class AtomicProgress {
private:
    std::atomic<size_t> current_;
    size_t total_;

public:
    AtomicProgress(size_t total)
        : current_(0)
        , total_(total)
    {
    }

    void increment(size_t count = 1) { current_.fetch_add(count, std::memory_order_relaxed); }

    size_t get_current() const { return current_.load(std::memory_order_relaxed); }

    size_t get_total() const { return total_; }

    float get_percentage() const
    {
        if (total_ == 0)
            return 100.0f;
        return (float)current_ / total_ * 100.0f;
    }

    bool is_complete() const { return current_.load(std::memory_order_relaxed) >= total_; }
};

}  // namespace MeshRepair
