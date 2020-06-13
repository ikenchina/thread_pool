# How to design a thread pool

[《中文版》](Design_thread_pool_zh.md)

## Why do we need a thread pool


Benefit of a thread pool over creating a new thread for each task is that thread creation and destruction overhead is restricted to the initial creation of the pool, which may result in better performance and better system stability. 

When a task is pushed to thread pool, thread pool pick a idle thread to execute the task.



Benefits of using a thread pool

- Avoid high resource overhead : reusing idle thread instead of creating a thread for every task.
- Reduce latency : when a task arrive, it can be executed immediately without waiting for thread to be created.
- Improve the manageability of threads: use thread pool for allocating threads uniformly, tuning, and monitoring.
- Extensibility : allowing to add more functions, for example, delayed tasks 



## Design a thread pool

The ideas of design thread pool are similar: push tasks into the blocking queue, and idle thread of the thread pool pop the task to execute. After completing the job, the thread is contained in the thread pool again.

Considering several features of thread pool:

- Queue : priority queue, FIFO queue, mixed queue, etc
- Pool size : the number of threads
- When and how to increase and decrease thread



### Task queue

The task queue of thread pool is a blocking queue, it blocks thread until one task is available.

Several types of task queue

- Priority queue : supports priority task
- Delay queue : exeucute a task at a specify time
- FIFO queue : first-in first-out
- Sync queue : the queue which push a task into queue will be blocking until the task is poped.




### The number of thread

> We are often asked : how many threads should be created?

Too many theads hurts performance : threads compete for scarce CPU and memory resources, resulting in higher memory usage and possible resource exhaustion. Impacts threads schedule, context switch, cache.  To the contrary, throughput suffers as processors go unused despite available work.


Categories of tasks:

- Compute intensive
- IO intensive or other blocking operations
- Combination

Reference to 《Java Concurrency in Practice》.

$$
N_{thread} = number\ of\ CPUs
$$

$$
U_{cpu} = CPU\ utilization
$$

$$
\frac{W}{C} = ratio\ of\ wait\ time\ to\ compute\ time
$$



**Compute intensive**    

$$
N_{thread} = N_{CPU} + 1
$$


Even compute-intensive threads occasionally take a page fault or pause for some other reason, so an “extra” runnable thread prevents CPU cycles from going unused when this happens.



**Optimal size if including IO or other blocking operations**

The optimal pool size for keeping the processors at the desired utilization is:
$$
N_{thread} = N_{cpu} * U_{cpu} * \left ( 1 + \frac{W}{C} \right )
$$
Of course, CPU cycles are not the only resource you might want to manage using thread pools. Other resources that can contribute to sizing constraints are memory, file handles, socket handles, and database connections. Calculating pool size constraints for these types of resources is easier: just add up how much of that resource each task requires and divide that into the total quantity available. The result will be an upper bound on the pool size.


### Thread creation and teardown

Creating some idle threads when thread pool is initialized.

**Scale out thread pool**   

Creating a new thread if a new task wouldn't be consumed within a duration and pool size is less than maximum pool size.


**Scale in thread pool**

Thread that has been idle for longer than the keep-alive time becomes a candidate for reaping and can be terminated if the current pool size exceeds the minimum pool size.



### Metris

Offer some metrics for monitoring

- task queue size
- pool size : the number of threads
- others



## Design a C++ thread pool


### Requirements



Requirements for a typical thread pool

- Threads
  - Setting the minimum and maximum pool size
  - Auto scale out/in thread pool
- Blocking queue
  - Used to store tasks
  - Blocking threads when there isn't an available task
  - Blocking thread puts a new task to queue when queue is full
- Task
  - Returning result after task has been executed
  - Execute a tash after a given delay




![image](https://gitee.com/ikenchina/img1/raw/master/1/others/9075b0eee736f9a8e46deb94d7f0eb84.png)


### Blocking quuee

Define queue interface to store tasks

```
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
```

Define FIFO queue inherited Queue

```
class FifoQueue : public Queue
```

Priority queue inherited Queue

```
class PriorityQueue : public Queue
```


Define blocking queue interface

```
class BlockingQueue
{
public:
    BlockingQueue() {Open();}
    virtual StatusCode Push(const Context& ctx, const Task& task) = 0;
    virtual StatusCode Pop(const Context& ctx, Task* t) = 0;
    virtual size_t QueueSize() = 0;
    virtual bool Empty() = 0;
    virtual void Clear() = 0;
    virtual void Close();
    virtual void Open();
};
```

Implement FIFO blocking queue

```
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
        
        
        // Wait until queue is available(not full, and not closed)
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
        in_queue_cond_.notify_one();  // wake up a thread
        return StatusCode::Ok;
    }
    
    virtual StatusCode Pop(const Context& ctx, Task* t)
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);		
		std::cv_status cv_status = std::cv_status::no_timeout;	
		// Wait when there isn't an available task
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
        out_queue_cond_.notify_one();   // Wake up a blocked thread
        
        *t = task;
        return StatusCode::Ok;
    }
    
    ......     
}
```



MixedBlockingQueue has FifoQueue and PriorityQueue, store fifo tasks and priority task respectively



```
class MixedBlockingQueue : public BlockingQueue
{
public:
    MixedBlockingQueue(size_t fifo_queue_size = 1000, size_t delay_queue_size=1000) :
    BlockingQueue(),
    fifo_queue_(std::make_shared<FifoQueue>(fifo_queue_size)), 
    delay_queue_(std::make_shared<PriorityQueue>(delay_queue_size))
    {
    }
    
    virtual StatusCode Push(const Context& ctx, const Task& task){
        // similar to BaseBlockingQueue
        ......
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

            // Check delay queue first
            if (!delay_queue_->Empty())
            {
                auto task = delay_queue_->Front();
                if (task.exec_time < std::chrono::system_clock::now())
                {      
                    delay_queue_->Pop();
                    *t = task;
                    break;
                }
                
                // Check fifo queue when there isn't available delay task
                if (!fifo_queue_->Empty())
                {
                    *t = fifo_queue_->Front();
                    fifo_queue_->Pop();
                    break;
                }
                
                // Wait until timeout or waked by other thread
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
    ......
}
```


### Configurations of thread pool

```
class ThreadPoolSettings
{
    // Minimum number of threads
	std::atomic<size_t> min_pool_size_;
	
	// Maximum number of threads
	std::atomic<size_t> max_pool_size_;
	
	// Terminating idle thread that has been idle for longer than the keepalive time.
	// Default value is 10 seconds.
	std::chrono::nanoseconds keepalive_time_ = std::chrono::seconds(10);
	
	// Create a new thread when a task wouldn't be consumed within this duration
	std::chrono::nanoseconds scale_pool_size_time_ = std::chrono::milliseconds(300);
};
```


### Thread pool

```
class ThreadPool {
public:
    ThreadPool(ThreadPoolSettings settings, 
    std::shared_ptr<BlockingQueue> queue = std::make_shared<MixedBlockingQueue>());、
    
	ThreadPool(const ThreadPool &) = delete;
	ThreadPool(ThreadPool &&) = delete;
	ThreadPool & operator=(const ThreadPool &) = delete;
	ThreadPool & operator=(ThreadPool &&) = delete;
	
	
	// Used to adjust settings dynamically
	ThreadPoolSettings& GetSettings() {return settings_;}
}
```

Start and stop

```
// Start thread pool and create minimum threads
void Start();

// Stop thread pool
// force is false : wait all tasks are completed
// force is true : clear all available tasks and wait in flight tasks are completed
void Stop(bool force = false);
```

Queues a task to thread pool

```
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
```

- ScheduleTask function
  - Variadic template for parameters of execution function of task
  - Return value : std::tuple
    - First element : status code
    - Second element : std::future for result of execution function of task


```
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
```



### Demo


```
int main(int argc, char* argv[])
{
    ThreadPoolSettings settings;
    auto increase_duration = std::chrono::milliseconds(50);
    
      settings.SetIdleThreadDuration(std::chrono::milliseconds(200)).SetMaxThreadSize(4).SetMinThreadSize(0);
    settings.SetIncreaseThreadDuration(increase_duration);

    ThreadPool tp(settings);
    tp.Start();
    
    auto ctx = Context::WithTimeout(Context(), std::chrono::seconds(1));

    auto resp = tp.ScheduleTask(ctx, [](int a) {return a*a;}, 2);
    if (std::get<0>(resp) != StatusCode::Ok)
        return 1;
    
    auto delay_resp = tp.ScheduleDelayTask(Context(), std::chrono::seconds(1), test_func, 3);
    if (std::get<0>(resp) != StatusCode::Ok)
        return 1;

    cout << "2 * 2 = " << std::get<1>(resp).get() << endl;
    cout << "3 * 3 = " << std::get<1>(delay_resp).get() << endl;

    tp.Stop();
}
```


### Repository


https://github.com/ikenchina/thread_pool