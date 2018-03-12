#pragma once

#include <condition_variable>
#include <queue>
#include <atomic>
#include <mutex>
#include <memory>
#include <iostream>
#include "common.h"

namespace kthreadpool {

    class Queue
    {
    public:
        virtual void Push(const Task& task) = 0;
        virtual void Pop() = 0;
        virtual Task Front() = 0;
        virtual bool Empty() = 0;
        virtual size_t Size() = 0;
        virtual void Clear() = 0;
        virtual bool Full() = 0;
    };

    class FifoQueue : public Queue
    {
    public:
        FifoQueue(size_t size = 1000)
        {
            max_size_ = size;
        }

        virtual void Push(const Task& task)
        {
            queue_.push(task);
        }

        virtual void Pop()
        {
            queue_.pop();
        }

        virtual Task Front()
        {
            return queue_.front();
        }

        virtual bool Empty()
        {
            return queue_.empty();
        }

        virtual void Clear()
        {
            std::queue<Task> t;
            queue_.swap(t);
        }

        virtual size_t Size()
        {
            return queue_.size();
        }
        
        virtual bool Full()
        {
            return queue_.size() >= max_size_;
        }

    private:
        std::queue<Task> queue_;
        size_t max_size_;
    };

    class PriorityQueue : public Queue
    {
    public:
        PriorityQueue(size_t size = 1000)
        {
            max_size_ = size;
        }

        virtual void Push(const Task& task)
        {
            queue_.push(task);
        }

        virtual void Pop()
        {
            queue_.pop();
        }

        virtual Task Front()
        {
            return queue_.top();
        }

        virtual bool Empty()
        {
            return queue_.empty();
        }

        virtual void Clear()
        {
            std::priority_queue<Task> t;
            queue_.swap(t);
        }

        virtual size_t Size()
        {
            return queue_.size();
        }

        virtual bool Full()
        {
            return queue_.size() >= max_size_;
        }

    private:
        std::priority_queue<Task> queue_;
        size_t max_size_;
    };


    class BlockingQueue
    {
    public:
        BlockingQueue() {Open();}
        virtual StatusCode Push(const Context& ctx, const Task& task) = 0;
        virtual StatusCode Pop(const Context& ctx, Task* t) = 0;
        virtual size_t QueueSize() = 0;
        virtual bool Empty() = 0;
        virtual void Clear() = 0;

        virtual void Close()
        {
            shutdown_ = true;
            in_queue_cond_.notify_all();
            out_queue_cond_.notify_all();
        }
        virtual void Open()
        {
            shutdown_ = false;
        }

    protected:
        std::atomic<bool> shutdown_;
        std::mutex queue_mutex_;
        std::condition_variable in_queue_cond_;
		std::condition_variable out_queue_cond_;
    };

    class BaseBlockingQueue : public BlockingQueue
    {
    public:
        BaseBlockingQueue(std::shared_ptr<Queue> q = std::make_shared<FifoQueue>()) : BlockingQueue()
        {
            queue_ = q;
        }

        virtual StatusCode Push(const Context& ctx, const Task& task)
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            while (queue_->Full() && !shutdown_)
            {
                if (out_queue_cond_.wait_until(lock, ctx.Deadline()) == std::cv_status::timeout)
                {
                    if (std::chrono::system_clock::now() >= ctx.Deadline())
					    return StatusCode::Timeout;
                }
            }
            if (shutdown_)
                return StatusCode::Closed;

            queue_->Push(task);
            in_queue_cond_.notify_one();
            return StatusCode::Ok;
        }

        virtual StatusCode Pop(const Context& ctx, Task* t)
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);		
			std::cv_status cv_status = std::cv_status::no_timeout;	
            if (queue_->Empty() && !shutdown_)
            {
                cv_status = in_queue_cond_.wait_until(lock, ctx.Deadline());
            }
            if (shutdown_ && queue_->Empty())
                return StatusCode::Closed;
            if (cv_status == std::cv_status::timeout)
                return StatusCode::Timeout;

            auto task = queue_->Front();
            queue_->Pop();
            out_queue_cond_.notify_one();
            
            *t = task;
            return StatusCode::Ok;
        }
                
        virtual size_t QueueSize() 
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);	
            return queue_->Size();
        }

        virtual bool Empty() 
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);	
            return queue_->Size() == 0;
        }

        virtual void Clear()
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_->Clear();
        }

    protected:
        std::shared_ptr<Queue> queue_;
    };


    class MixedBlockingQueue : public BlockingQueue
    {
    public:
        MixedBlockingQueue(size_t fifo_queue_size = 1000, size_t delay_queue_size=1000) :
        BlockingQueue(),
        fifo_queue_(std::make_shared<FifoQueue>(fifo_queue_size)), 
        delay_queue_(std::make_shared<PriorityQueue>(delay_queue_size))
        {
        }

        virtual StatusCode Push(const Context& ctx, const Task& task)
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            auto queue = task.Delayed() ? delay_queue_ : fifo_queue_;
            while(queue->Full() && !shutdown_)
            {
                if (out_queue_cond_.wait_until(lock, ctx.Deadline()) == std::cv_status::timeout)
                {
                    if (std::chrono::system_clock::now() >= ctx.Deadline())
                        return StatusCode::Timeout;
                }
            }
            if (shutdown_)
                return StatusCode::Closed;
            queue->Push(task);
            in_queue_cond_.notify_one();
            return StatusCode::Ok;
        }

        virtual StatusCode Pop(const Context& ctx, Task* t)
        {
            while(true)
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);		
                std::cv_status cv_status = std::cv_status::no_timeout;
                if (isEmpty() && !shutdown_)
                {
                    cv_status = in_queue_cond_.wait_until(lock, ctx.Deadline());
                }
                if (shutdown_ && isEmpty())
                    return StatusCode::Closed;
                if (cv_status == std::cv_status::timeout)
                    return StatusCode::Timeout;

                // get task from delay_queue first.
                if (!delay_queue_->Empty())
                {
                    auto task = delay_queue_->Front();
                    if (task.exec_time < std::chrono::system_clock::now())
                    {      
                        delay_queue_->Pop();
                        *t = task;
                        break;
                    }
                    if (!fifo_queue_->Empty())
                    {
                        *t = fifo_queue_->Front();
                        fifo_queue_->Pop();
                        break;
                    }
                    auto stat = in_queue_cond_.wait_until(lock, std::min(task.exec_time, ctx.Deadline()));
                    if (stat == std::cv_status::timeout)
                    {
                        if (ctx.Deadline() < std::chrono::system_clock::now())
                            return StatusCode::Timeout;
                    }
                    continue;
                }
                if (!fifo_queue_->Empty())
                {
                    *t = fifo_queue_->Front();
                    fifo_queue_->Pop();
                    break;
                }
            }
            out_queue_cond_.notify_one();
            return StatusCode::Ok;
        }
                
        virtual size_t QueueSize() 
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);	
            return fifo_queue_->Size() + delay_queue_->Size();
        }

        virtual bool Empty() 
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);	
            return isEmpty();
        }

        virtual void Clear()
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            fifo_queue_->Clear();
            delay_queue_->Clear();
            in_queue_cond_.notify_all();
            out_queue_cond_.notify_all();
        }

        size_t FifoQueueSize()
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);	
            return fifo_queue_->Size();
        }
        
        size_t DelayQueueSize()
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);	
            return delay_queue_->Size();
        }

    protected:
        
        bool isEmpty()
        {
            return fifo_queue_->Empty() && delay_queue_->Empty();
        }

    protected:
        std::shared_ptr<Queue> fifo_queue_;
        std::shared_ptr<Queue> delay_queue_;
    };
}