#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace MeshRepair {

template<typename T> class BoundedQueue {
public:
    explicit BoundedQueue(size_t /*max_memory_bytes*/)
        : finished_(false)
    {
    }

    void push(T item)
    {
        {
            std::unique_lock<std::mutex> lock(mtx_);
            queue_.push(std::move(item));
        }
        cv_pop_.notify_one();
    }

    bool pop(T& item)
    {
        std::unique_lock<std::mutex> lock(mtx_);
        cv_pop_.wait(lock, [this] { return finished_ || !queue_.empty(); });
        if (queue_.empty()) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    void finish()
    {
        {
            std::unique_lock<std::mutex> lock(mtx_);
            finished_ = true;
        }
        cv_pop_.notify_all();
    }

private:
    std::queue<T> queue_;
    std::mutex mtx_;
    std::condition_variable cv_pop_;
    bool finished_;
};

struct ThreadingConfig {
    size_t num_threads = 0;  // 0 = auto (hw_cores / 2)
    size_t queue_size  = 10;
    bool verbose       = false;

    size_t detection_threads = 0;
    size_t filling_threads   = 0;
    size_t hw_cores          = 0;
};

class ThreadPool {
public:
    ThreadPool(size_t threads = 0)
        : stop_(false)
    {
        start_workers(threads);
    }

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&& other) noexcept
        : workers_(std::move(other.workers_))
        , tasks_(std::move(other.tasks_))
        , stop_(other.stop_)
    {
    }
    ThreadPool& operator=(ThreadPool&& other) noexcept
    {
        if (this != &other) {
            stop_    = other.stop_;
            workers_ = std::move(other.workers_);
            tasks_   = std::move(other.tasks_);
        }
        return *this;
    }

    template<class F, class... Args> bool enqueue(F&& f, Args&&... args)
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) {
                return false;
            }
            tasks_.emplace(
                [fn = std::forward<F>(f), tuple_args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                    std::apply(fn, std::move(tuple_args));
                });
        }
        condition_.notify_one();
        return true;
    }

    size_t threadCount() const { return workers_.size(); }

    void resize(size_t new_thread_count)
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        join_all();
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = false;
            clear_queue();
        }
        start_workers(new_thread_count);
    }

    ~ThreadPool()
    {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        join_all();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_;

    void start_workers(size_t count)
    {
        for (size_t i = 0; i < count; ++i) {
            workers_.emplace_back([this] { worker_function(); });
        }
    }

    void join_all()
    {
        for (auto& w : workers_) {
            if (w.joinable()) {
                w.join();
            }
        }
        workers_.clear();
    }

    void clear_queue()
    {
        std::queue<std::function<void()>> empty;
        std::swap(tasks_, empty);
    }

    void worker_function()
    {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                if (stop_ && tasks_.empty()) {
                    return;
                }
                task = std::move(tasks_.front());
                tasks_.pop();
            }

            try {
                task();
            } catch (...) {
            }
        }
    }
};

struct ThreadManager {
    ThreadingConfig config;
    ThreadPool detection_pool;
    ThreadPool filling_pool;
};

void
thread_manager_init(ThreadManager& mgr, const ThreadingConfig& cfg);
void
thread_manager_enter_detection(ThreadManager& mgr);
void
thread_manager_enter_pipeline(ThreadManager& mgr);
void
thread_manager_enter_filling(ThreadManager& mgr);

size_t
get_hardware_cores();
size_t
get_default_thread_count();

}  // namespace MeshRepair
