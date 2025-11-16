#ifndef MESHREPAIR_THREAD_SAFE_COUT_H
#define MESHREPAIR_THREAD_SAFE_COUT_H

#include <iostream>
#include <sstream>
#include <mutex>

namespace MeshRepair {

// Thread-safe cout wrapper
// Usage: thread_safe_cout() << "message" << value << "\n";
class ThreadSafeCout {
private:
    static std::mutex& get_mutex()
    {
        static std::mutex cout_mutex;
        return cout_mutex;
    }

    std::ostringstream buffer_;

public:
    ThreadSafeCout() = default;

    template<typename T> ThreadSafeCout& operator<<(const T& value)
    {
        buffer_ << value;
        return *this;
    }

    // Handle stream manipulators like std::endl, std::flush
    ThreadSafeCout& operator<<(std::ostream& (*manip)(std::ostream&))
    {
        buffer_ << manip;
        return *this;
    }

    ~ThreadSafeCout()
    {
        std::lock_guard<std::mutex> lock(get_mutex());
        std::cout << buffer_.str() << std::flush;
    }
};

// Helper function for convenience
inline ThreadSafeCout
thread_safe_cout()
{
    return ThreadSafeCout();
}

// Same for cerr
class ThreadSafeCerr {
private:
    static std::mutex& get_mutex()
    {
        static std::mutex cerr_mutex;
        return cerr_mutex;
    }

    std::ostringstream buffer_;

public:
    ThreadSafeCerr() = default;

    template<typename T> ThreadSafeCerr& operator<<(const T& value)
    {
        buffer_ << value;
        return *this;
    }

    ThreadSafeCerr& operator<<(std::ostream& (*manip)(std::ostream&))
    {
        buffer_ << manip;
        return *this;
    }

    ~ThreadSafeCerr()
    {
        std::lock_guard<std::mutex> lock(get_mutex());
        std::cout << buffer_.str() << std::flush;
    }
};

inline ThreadSafeCerr
thread_safe_cerr()
{
    return ThreadSafeCerr();
}

}  // namespace MeshRepair

#endif  // MESHREPAIR_THREAD_SAFE_COUT_H
