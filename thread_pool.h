#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <queue>
#include <unordered_map>
#include <future>
#include <tuple>

#include "common.h"
#include "blocking_queue.h"

namespace kthreadpool {

	class ThreadPoolSettings
	{
	public:
		// Default value is 1
		ThreadPoolSettings& SetMinPoolSize(size_t t)
		{
			min_pool_size_ = t;
			return *this;
		}

		// Default value is the number of CPU cores + 1
		ThreadPoolSettings& SetMaxPoolSize(size_t t)
		{
			max_pool_size_ = t;
			return *this;
		}

		// Terminating idle thread that has been idle for longer than the keepalive time.
		// Default value is 10 seconds.
		ThreadPoolSettings& SetKeepaliveTime(std::chrono::nanoseconds keepalive_time)
		{
			keepalive_time_ = keepalive_time;
			return *this;
		}

		// Creating a new thread if a new task wouldn't be consumed within a duration 
		//   and pool size is less than `SetMaxPoolSize`.
		// Default value is 300 milli seconds
		ThreadPoolSettings& SetScaleoutTime(std::chrono::nanoseconds duration)
		{
			scale_pool_size_time_ = duration;
			return *this;
		}

		size_t MinPoolSize() {return min_pool_size_.load();}
		size_t MaxPoolSize() {return max_pool_size_.load();}
		std::chrono::nanoseconds KeepaliveTime() {return keepalive_time_;}
		std::chrono::nanoseconds ScaleoutTime() {return scale_pool_size_time_;}

		ThreadPoolSettings() 
		{
			min_pool_size_ = 1;
			max_pool_size_ = std::thread::hardware_concurrency() + 1;
		}
		
		ThreadPoolSettings(const ThreadPoolSettings& set) 
		{
			min_pool_size_ = set.min_pool_size_.load();
			max_pool_size_ = set.max_pool_size_.load();
			keepalive_time_ = set.keepalive_time_;
			scale_pool_size_time_ = set.scale_pool_size_time_;
		}

		ThreadPoolSettings& operator=(const ThreadPoolSettings& set) 
		{
			min_pool_size_ = set.min_pool_size_.load();
			max_pool_size_ = set.max_pool_size_.load();
			keepalive_time_ = set.keepalive_time_;
			scale_pool_size_time_ = set.scale_pool_size_time_;
			return *this;
		}

	private:
		std::atomic<size_t> min_pool_size_;
		std::atomic<size_t> max_pool_size_;
		std::chrono::nanoseconds keepalive_time_ = std::chrono::seconds(10);
		std::chrono::nanoseconds scale_pool_size_time_ = std::chrono::milliseconds(300);

		friend class ThreadPool;
	};

	class ThreadPool {
		
	protected:
		class Worker;
		class TimeTask;
	public:
		ThreadPool(ThreadPoolSettings settings, std::shared_ptr<BlockingQueue> queue = std::make_shared<MixedBlockingQueue>());
		~ThreadPool();

		ThreadPool(const ThreadPool &) = delete;
		ThreadPool(ThreadPool &&) = delete;
		ThreadPool & operator=(const ThreadPool &) = delete;
		ThreadPool & operator=(ThreadPool &&) = delete;

		// used to adjust thread pool size dynamically
		ThreadPoolSettings& GetSettings() {return settings_;}

		// Start : start worker threads
		void Start();

		// Stop : stop all worker threads and waiting to complete all tasks.
		// @param force : clear all pending tasks if true
		void Stop(bool force = false);

		// ScheduleTask : schedule a task to thread pool.
		// return : 
		//         status code. 
		//         std::future : returned value from function F
		template<class F, class... Args>
		auto ScheduleTask(const Context& ctx, F&& f, Args&&... args) 
			-> std::tuple<StatusCode, std::future<typename std::result_of<F(Args...)>::type>>
		{
			using return_type = typename std::result_of<F(Args...)>::type;

			auto task = std::make_shared<std::packaged_task<return_type()> >(
				std::bind(std::forward<F>(f), std::forward<Args>(args)...)
			);
				
			std::future<return_type> res = task->get_future();
			auto tt = Task([task](){(*task)();});
			auto ret = scheduleTask(ctx, tt);
			return std::make_tuple(ret, std::move(res));
		}

		// ScheduleDelayTask : schedule a delay task to thread pool.
		template<class D, class F, class... Args>
		auto ScheduleDelayTask(const Context& ctx, D delay_duration, F&& f, Args&&... args) 
			-> std::tuple<StatusCode, std::future<typename std::result_of<F(Args...)>::type>>
		{
			using return_type = typename std::result_of<F(Args...)>::type;

			auto task = std::make_shared<std::packaged_task<return_type()> >(
				std::bind(std::forward<F>(f), std::forward<Args>(args)...)
			);
				
			std::future<return_type> res = task->get_future();
			auto tt = Task([task](){(*task)();}, std::chrono::system_clock::now() + delay_duration);
			auto ret = scheduleTask(ctx, tt);
			return std::make_tuple(ret, std::move(res));
		}

		// the number of threads
		size_t PoolSize();

	protected:
		std::chrono::system_clock::time_point TimeAddDuration(std::chrono::system_clock::time_point tp, 
                            std::chrono::nanoseconds duration);

		StatusCode scheduleTask(const Context& ctx, Task& t);
		void Run(Worker* w);
		void TryIncreaseThreads();
		void Execute(const Task& task);
		bool needGcIdleWorkers();
		void GcIdleWorkers();
		void GcWorkers();
		void CreateThread();
	
	protected:
		
		ThreadPoolSettings settings_;
		std::mutex thread_mutex_;
		std::unordered_map<int64_t, std::shared_ptr<Worker>> workers_;
		std::atomic_uint available_workers_size_;
		std::atomic_bool need_gc_idle_workers_;
		std::atomic<int64_t> thread_id_generator_;
		std::shared_ptr<BlockingQueue> queue_;
		const std::chrono::system_clock::time_point max_system_clock_ = std::chrono::time_point<std::chrono::system_clock>::max();
	
	protected:

		class Worker
		{
		public:
			Worker(ThreadPool *p, int64_t id);
			~Worker();
			void Start();
			bool Shutdown();
			void Wait();
			int64_t GetId() const;
			bool IsShutdown();

			Worker(const Worker&) = delete;
			const Worker& operator=(const Worker&) = delete;
			Worker(Worker&) = delete;
			const Worker& operator=(Worker&) = delete;
		private:
			void Run();

			ThreadPool* pool_;
			std::shared_ptr<std::thread> thread_;
			std::atomic_bool shutdown_;
			int64_t id_;
		};
	};
}  // namespace 

