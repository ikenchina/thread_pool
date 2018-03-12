#include "thread_pool.h"
#include <assert.h>
#include <sstream>
#include <functional>
#include <cassert>

namespace kthreadpool {

	ThreadPool::Worker::Worker(ThreadPool *p, int64_t id) : pool_(p), id_(id) 
	{
		shutdown_ = true;
	}

	ThreadPool::Worker::~Worker()
	{}

	void ThreadPool::Worker::Start()
	{
		bool expect = true;
		if (shutdown_.compare_exchange_strong(expect, false))
			thread_ = std::make_shared<std::thread>(std::thread(std::bind(&Worker::Run, this)));
	}

	bool ThreadPool::Worker::Shutdown()
	{
		bool expect = false;
		return shutdown_.compare_exchange_strong(expect, true);
	}

	void ThreadPool::Worker::Wait()
	{
		if (thread_->joinable())
			thread_->join();
	}

	bool ThreadPool::Worker::IsShutdown()
	{
		return shutdown_;
	}

	void ThreadPool::Worker::Run()
	{
		pool_->Run(this);
	}

	int64_t ThreadPool::Worker::GetId() const
	{
		return id_;
	}

	// ThreadPool
	ThreadPool::ThreadPool(ThreadPoolSettings settings,std::shared_ptr<BlockingQueue> queue)
		: settings_(settings), available_workers_size_(0), 
		need_gc_idle_workers_(false), thread_id_generator_(0), queue_(queue)
	{
	}

	ThreadPool::~ThreadPool() 
	{
		Stop();
	}

	void ThreadPool::Start() 
	{
		std::unique_lock<std::mutex> lock(thread_mutex_);

		assert(workers_.empty());
		queue_->Open();

		for (size_t i = 0; i < settings_.MinPoolSize(); ++i) 
		{
			available_workers_size_++;
			auto id = ++thread_id_generator_;
			auto w = std::make_shared<Worker>(this, id);
			workers_[w->GetId()] = w;
			w->Start();
		}
	}

	void ThreadPool::Stop(bool force) 
	{
		queue_->Close();
		if (force)
			queue_->Clear();
		GcWorkers();
	}

	size_t ThreadPool::PoolSize()
	{
		return available_workers_size_;
	}

	StatusCode ThreadPool::scheduleTask(const Context& ctx, Task& task)
	{
		StatusCode res = StatusCode::Ok;

		while(true)
		{
			Context ctx2 = Context::WithDeadline(ctx, TimeAddDuration(std::chrono::system_clock::now(), settings_.ScaleoutTime()));
			if (ctx.Deadline() < ctx2.Deadline())
			{
				ctx2.SetDeadline(ctx.Deadline());
			}
			
			res = queue_->Push(ctx2, task);

			if (needGcIdleWorkers())
				GcIdleWorkers();

			if (available_workers_size_ == 0)
			{
				uint expected = 0; 
				std::unique_lock<std::mutex> lock(thread_mutex_);
				if (available_workers_size_.compare_exchange_strong(expected, 1))
					CreateThread();
			}

			if (res != StatusCode::Timeout)
				break;

			if (std::chrono::system_clock::now() >= ctx.Deadline())
				break;

			TryIncreaseThreads();
		}

		return res;
	}

	void ThreadPool::TryIncreaseThreads()
	{
		std::unique_lock<std::mutex> lock(thread_mutex_);
		if (workers_.size() >= settings_.MaxPoolSize())
			return;
		available_workers_size_++;
		CreateThread();
	}

	void ThreadPool::CreateThread()
	{
		auto id = ++thread_id_generator_;
		auto w = std::make_shared<Worker>(this, id);
		workers_[w->GetId()] = w;
		w->Start();
	}

	void ThreadPool::Execute(const Task& task)
	{
		if (task.func)
			(task.func)();
	}

	bool ThreadPool::needGcIdleWorkers()
	{
		bool expected = true;
		return need_gc_idle_workers_.compare_exchange_strong(expected, false);
	}

	void ThreadPool::GcIdleWorkers()
	{
		std::vector<std::shared_ptr<Worker>> gc_workers;
		{
			std::unique_lock<std::mutex> lock(thread_mutex_);
			for (auto it = workers_.begin(); it != workers_.end();)
			{
				if (it->second->IsShutdown())
				{
					gc_workers.push_back(it->second);
					it = workers_.erase(it);
				} else {
					it++;
				}
			}
		}
		for (auto& w : gc_workers)
		{
			w->Wait();
		}
	}

	void ThreadPool::GcWorkers()
	{
		std::vector<std::shared_ptr<Worker>> gc_workers;
		{
			std::unique_lock<std::mutex> lock(thread_mutex_);
			for (auto it = workers_.begin(); it != workers_.end(); it++)
			{
				gc_workers.push_back(it->second);
				it->second->Shutdown();
			}
			workers_.clear();
		}
		for (auto& w : gc_workers)
		{
			w->Wait();
		}
	}

	void ThreadPool::Run(Worker* worker)
	{
		while(true)
		{
			Task task;
			auto res = queue_->Pop(Context::WithTimeout(Context(), settings_.KeepaliveTime()), &task);
			if (res == StatusCode::Closed)
			{
				available_workers_size_--;
				return;
			} else if (res == StatusCode::Timeout)
			{
				std::unique_lock<std::mutex> lock(thread_mutex_);
				if (available_workers_size_ > settings_.MinPoolSize())
				{
					if (available_workers_size_ == 1 && queue_->QueueSize() > 0)
						continue;
					available_workers_size_--;
					worker->Shutdown();
					need_gc_idle_workers_=true;
					return;
				}
			} else {
				Execute(task);
			}
		}
	}

	std::chrono::system_clock::time_point ThreadPool::TimeAddDuration(std::chrono::system_clock::time_point tp, 
							std::chrono::nanoseconds duration)
	{
		if (std::chrono::time_point<std::chrono::system_clock>::max() - duration > tp)
			tp = tp + duration; 
		else
			tp = std::chrono::time_point<std::chrono::system_clock>::max();
		return tp;
	}
}  // namespace kThreadPool
