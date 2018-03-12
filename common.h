#pragma once

#include <chrono>
#include <functional>

namespace kthreadpool {

    enum StatusCode
    {
        Ok = 1,
        Timeout = -0x2,
        Closed = -0x3,
    };

    class Context
    {
    public:
        Context() {deadline_ = std::chrono::time_point<std::chrono::system_clock>::max();}

        static Context WithTimeout(const Context& ctx = Context(), std::chrono::nanoseconds t = std::chrono::nanoseconds::max())
        {
            Context ctx2 = ctx;
            ctx2.deadline_ = std::chrono::system_clock::now() + t;
            return ctx2;
        }

        static Context WithDeadline(const Context& ctx = Context(), std::chrono::system_clock::time_point deadline = std::chrono::time_point<std::chrono::system_clock>::max())
        {
            Context ctx2 = ctx;
            ctx2.deadline_ = deadline;
            return ctx2;
        }

        std::chrono::system_clock::time_point Deadline() const 
        {
            return deadline_;
        }

        void SetDeadline(std::chrono::system_clock::time_point deadline)
        {
            deadline_ = deadline;
        }

    private:
        std::chrono::system_clock::time_point deadline_;
    };


    typedef std::function<void()> TaskFunc;

    class Task
    {
    public:
        Task()  {}
        Task(TaskFunc f) : func(f) {}
        Task(TaskFunc f, std::chrono::system_clock::time_point tp) : func(f), exec_time(tp) {} 

        bool operator()() const {
            return func != nullptr;
        }
        bool operator<(const Task& task) const 
        {
            return exec_time > task.exec_time;
        }
        bool Delayed() const
        {
            return exec_time != std::chrono::system_clock::from_time_t(time_t(0));
        }
        TaskFunc func = nullptr;
        std::chrono::system_clock::time_point exec_time = std::chrono::system_clock::from_time_t(time_t(0));
    };
}
