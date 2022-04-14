# 如何设计一个线程池

## 为什么需要线程池



线程的频繁创建和销毁，不仅会消耗系统资源，还会降低系统的稳定性。


线程池预先创建空闲的线程，程序将一个任务传给线程池，线程池就会启动空闲线程来执行这个任务，执行结束以后，该线程并不会销毁，而是再次返回线程池中成为空闲状态，等待执行下一个任务。


使用线程池好处：

- 降低资源消耗：通过池化技术重复利用已创建的线程，降低线程创建和销毁造成的损耗。
- 提高响应速度：任务到达时，无需等待线程创建即可立即执行。
- 提高线程的可管理性：线程是稀缺资源，如果无限制创建，不仅会消耗系统资源，还会因为线程的不合理分布导致资源调度失衡，- 降低系统的稳定性。使用线程池可以进行统一的分配、调优和监控。
- 提供更多更强大的功能：线程池具备可拓展性，允许开发人员向其中增加更多的功能。比如延时定时线程池，就允许任务延期执行或定期执行。



## 设计线程池

线程池设计的思路都大同小异：将任务写入到阻塞队列中，然后线程池中的空闲线程从队列中获取任务执行，执行完成后再从队列获取新的任务执行。




设计线程池需要考虑的几个特性

- 任务队列：按照功能分为优先级队列，先进后出队列，同步队列等等
- 线程数量的控制
- 线程数量增加或回收



### 任务队列

线程池中任务队列都是阻塞型的，线程从中获取任务，没有任务则阻塞。   
一般有几种特性的队列

- 优先级队列：支持有优先级的任务
- 延迟队列：支持任务可以在某个时间点执行
- 先进后出队列：队列类似于栈一样，任务先进后出
- 同步队列：不存储任务的队列，将任务push到队列中时阻塞，直到将其分配给idle线程



### 线程数量

> 多线程编程中，经常会被问到：该设置多少个线程才合理？

如果线程数设置太多，则对线程调度，上下文切换，缓存等产生较大的影响；如果设置太小，则造成吞吐不够。    
而到底设置多少线程数，其实和机器资源的额度和任务特征有关。   
我们一般将任务分为几种

- 计算密集型
- IO密集型
- 混合型：计算与IO密集型


引用《Java Concurrency in Practice》中的公式来估算合适的线程数。



**计算密集型**

`线程数 = CPU数 + 1`

如果一个线程产生缺页或者其他原因停止运行，则还有一个idle线程会占用CPU，不会导致CPU资源浪费。但是这只是一个相对合理的经验值，并不科学。



**IO密集型和混合型**
$$
N_{thread} = number\ of\ CPUs
$$

$$
U_{cpu} = CPU\ utilization
$$

$$
\frac{W}{C} = ratio\ of\ wait\ time\ to\ compute\ time
$$





如果希望将CPU达到指定的利用率，则按照以下公式来计算线程数
$$
N_{thread} = N_{cpu} * U_{cpu} * \left ( 1 + \frac{W}{C} \right )
$$




所以，最佳线程数并不一定是固定不变的，可以随着任务的差异及资源利用率进行动态调整，这就需要线程池有动态调整线程的能力。



### 线程的创建与回收

线程池一般会在初始化时创建空闲线程，或者是等到需要的时候再延迟创建。


**线程池扩容**   
当任务较多，已有线程都处于运行状态，而已有线程数没有达到最大线程数限制时，则需要创建新的线程来执行任务。   
也会有一些触发条件，如任务等待多久仍然没有被调度则创建新线程等。

**线程池缩容**

当线程处于空闲状态一段时间后，则需要回收此空闲线程以节约资源。



### 线程池监控指标

线程池需要能够导出一些指标以观测其健康状况，如

- 列队中任务数量
- 运行中线程数量



## C++实现线程池


### 需求

在开发一个C++线程池前，我们先明确几点需求

- 线程数
  - 能够指定最小和最大线程数
  - 任务等待多久才开始创建新线程
  - 线程空闲多久才开始回收线程
- 阻塞队列
  - 用来存储任务
  - 当队列为空时，从中获取任务的操作会被阻塞直到有其他线程写入新任务
  - 当队列已满时，将任务写入队列的操作会被阻塞直到有其他线程从队列中获取任务
- 任务
  - 能够获取任务执行后的返回值
  - 不固化任务的函数签名
  - 任务能够延迟执行


明确需求后，线程池的设计就比较清晰了


![image](https://gitee.com/ikenchina/img1/raw/master/1/others/9075b0eee736f9a8e46deb94d7f0eb84.png)


### 阻塞队列

先定义队列的接口，用来存储任务

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

实现普通队列和优先级队列

```
class FifoQueue : public Queue
```

```
class PriorityQueue : public Queue
```


再定义阻塞队列的接口

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


实现普通阻塞队列，默认使用FifoQueue 来存储任务。

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
        // 当队列已满，且没有关闭，则等待有线程从队列中获取任务
        while (queue_->Full() && !shutdown_)
        {
            if (out_queue_cond_.wait_until(lock, ctx.Deadline()) == std::cv_status::timeout)
            {
                // 如果等待超时且超过deadline，则返回Timeout
                if (std::chrono::system_clock::now() >= ctx.Deadline())
				    return StatusCode::Timeout;
            }
        }
        if (shutdown_)
            return StatusCode::Closed;

        queue_->Push(task);
        in_queue_cond_.notify_one();  // 唤醒一个线程
        return StatusCode::Ok;
    }
    
    virtual StatusCode Pop(const Context& ctx, Task* t)
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);		
		std::cv_status cv_status = std::cv_status::no_timeout;	
		// 如果队列为空且没有关闭，则等待（有任务写入队列）
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
        out_queue_cond_.notify_one();   // 唤醒一个被阻塞线程
        
        *t = task;
        return StatusCode::Ok;
    }
    
    ......     
}
```

混合队列继承于BlockingQueue，其分别使用FifoQueue和PriorityQueue来存储普通任务和延迟执行任务。

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
        // 和BaseBlockingQueue几乎一样
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

            // 先从delay queue获取，
            // delay queue 对执行时间更为敏感
            if (!delay_queue_->Empty())
            {
                auto task = delay_queue_->Front();
                if (task.exec_time < std::chrono::system_clock::now())
                {      
                    delay_queue_->Pop();
                    *t = task;
                    break;
                }
                // 若task的执行时间还早，则检查fifo queue
                if (!fifo_queue_->Empty())
                {
                    *t = fifo_queue_->Front();
                    fifo_queue_->Pop();
                    break;
                }
                
                // 等待，直到到达task执行时间或者被其他线程唤醒
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


### 线程池配置

```
class ThreadPoolSettings
{
    // 最小线程数
	std::atomic<size_t> min_pool_size_;
	
	// 最大线程数
	std::atomic<size_t> max_pool_size_;
	
	// 空闲线程时间：当超过此阈值线程仍然无法获取到任务，则销毁此线程
	std::chrono::nanoseconds keepalive_time_ = std::chrono::seconds(10);
	
	// 线程扩容时间：当Push任务到队列且队列已满时，等待超过此阈值则进行新建一个新线程
	std::chrono::nanoseconds scaleout_time_ = std::chrono::milliseconds(300);
};
```


### 线程池

```
class ThreadPool {
public:
    // 构造函数
    // 传入配置和阻塞队列（默认使用混合队列）
    ThreadPool(ThreadPoolSettings settings, 
    std::shared_ptr<BlockingQueue> queue = std::make_shared<MixedBlockingQueue>());、
    
    // 禁止拷贝
	ThreadPool(const ThreadPool &) = delete;
	ThreadPool(ThreadPool &&) = delete;
	ThreadPool & operator=(const ThreadPool &) = delete;
	ThreadPool & operator=(ThreadPool &&) = delete;
	
	
	// 用于动态调整线程池配置
	ThreadPoolSettings& GetSettings() {return settings_;}
}
```

启动与停止

```
// 启动线程池，创建最少idle 线程
void Start();

// 停止线程池，回收所有线程
// 如果force=false：等待所有任务执行完成，再回收线程
// 如果force=true：等待正在执行的任务执行完，再回收线程
void Stop(bool force = false);
```

提交任务

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

- 函数签名
  - 参数：利用可变模板来支持任意形参
  - 返回值：函数返回一个`std::tuple`，
    - 第一个元素是状态码StatusCode，
    - 第二个元素是任务返回值的std::future


```
StatusCode ThreadPool::scheduleTask(const Context& ctx, Task& task)
{
	StatusCode res = StatusCode::Ok;

	while(true)
	{
	    // Context的Deadline 和 IncreaseThreadDuration 中较小者作为 Push队列的超时时间
		Context ctx2 = Context::WithDeadline(ctx, TimeAddDuration(std::chrono::system_clock::now(), settings_.IncreaseThreadDuration()));
		if (ctx.Deadline() < ctx2.Deadline())
		{
			ctx2.SetDeadline(ctx.Deadline());
		}
		
		res = queue_->Push(ctx2, task);

        // 如果需要回收线程
		if (needGcIdleWorkers())
			GcIdleWorkers();

        // 如果线程池还没有线程，则新建一个
		if (available_workers_size_ == 0)
		{
			uint expected = 0; 
			std::unique_lock<std::mutex> lock(thread_mutex_);
			if (available_workers_size_.compare_exchange_strong(expected, 1))
				CreateThread();
		}

		if (res != StatusCode::Timeout)
			break;

        // 如果超过Context的Deadline
		if (std::chrono::system_clock::now() >= ctx.Deadline())
			break;
        
        // 尝试新建一个线程
		TryIncreaseThreads();
	}

	return res;
}
```



### 使用


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


### 完整代码


https://github.com/ikenchina/thread_pool