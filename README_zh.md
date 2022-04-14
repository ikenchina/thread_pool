# C++ 线程池

《[如何设计一个线程池](Design_thread_pool_zh.md)》


## 介绍

一个轻量级，通用，纯C++实现的线程池。

支持如下特性：
- 普通任务：提交任务给线程池后，线程池会立即执行
- 延迟任务：线程池在指定时间点执行
- 动态对线程池进行扩容、缩容


## 设计

### APIs

#### 枚举

```
enum StatusCode
{
    Ok = 1,             // 
    Timeout = -0x2,     // schedule timeout, 
    Closed = -0x3,      // thread pool is closed
};
```

#### 类

##### **上下文**

**WithTimeout**

```
static Context WithTimeout(const Context& ctx = Context(), std::chrono::nanoseconds t = std::chrono::nanoseconds::max())
```
创建一个有超时信息的上下文


**WithDeadline**

```
static Context WithDeadline(const Context& ctx = Context(), std::chrono::system_clock::time_point deadline = std::chrono::time_point<std::chrono::system_clock>::max())
```
创建一个有限期的上下文


##### 线程池设置


**SetMinPoolSize**

```
ThreadPoolSettings& SetMinPoolSize(size_t t)
```
设置最小线程池，默认是1。


**SetMaxPoolSize**

```
ThreadPoolSettings& SetMaxPoolSize(size_t t)
```
设置最大线程池，默认是CPU核心数+1


**SetMaxQueueSize**

```
ThreadPoolSettings& SetMaxQueueSize(size_t t)
```
最长队列，超过队列`ScheduleTask`将会被阻塞，默认是1000。



**SetMaxDelayQueueSize**

```
ThreadPoolSettings& SetMaxDelayQueueSize(size_t t)
```
设置最大延迟队列，超过队列`ScheduleDelayTask`将会被阻塞。



**SetKeepaliveTime**

```
ThreadPoolSettings& SetKeepaliveTime(std::chrono::nanoseconds idle_duration)
```
设置最大keepalive时间，若一个idle线程超过此时间没有执行任务则会被回收，默认10秒。



**SetScaleoutTime**

```
ThreadPoolSettings& SetScaleoutTime(std::chrono::nanoseconds duration)
```
如果队列满了，且等待一段时间后任务仍然没有被调度，则进行扩容，默认是300微秒。




##### BlockingQueue

**Push**

```
virtual StatusCode Push(const Context& ctx, const Task& task) = 0;
```
将任务入队列。如果队列满了，则会被阻塞直到Context超时。


**Pop**

```
virtual StatusCode Pop(const Context& ctx, Task* t) = 0;
```
从队列中移除出一个任务，如果队列没有任务，则会被阻塞直到Context超时。



**Clear**

```
virtual void Clear() = 0;
```
清除队列中所有的任务。




##### BaseBlockingQueue

```
BaseBlockingQueue(std::shared_ptr<Queue> queue = std::make_shared<FifoQueue>())
```
创建一个阻塞队列，默认是 先进先出队列。


##### MixedBlockingQueue

```
MixedBlockingQueue(size_t fifo_queue_size = 1000, size_t delay_queue_size=1000)
```
创建一个混合队列，由 先进先出队列和延迟队列组成。




##### 线程池


**构造函数**

```
ThreadPool(ThreadPoolSettings settings, std::shared_ptr<BlockingQueue> queue = std::make_shared<MixedBlockingQueue>());
```
创建一个线程池。

参数：
- 配置
- 阻塞队列


**启动线程池**

```
void Start();
```


**停止线程池**

```
void Stop(bool force=false);
```
force :
- False ： 等待线程池处理完队列中所有任务
- True ： 等待线程池处理完正在处理的任务，队列中的任务丢弃。



**ScheduleTask**

```
template<class F, class... Args>
auto ScheduleTask(const Context& ctx, F&& function, Args&&... args) 
        -> std::tuple<StatusCode, std::future<typename std::result_of<F(Args...)>::type>>
```
将一个任务入队列
- 上下文
  - `Context()` ： 阻塞直到任务完成入队列
  - `Context::WithTimeout(Context(), duration_timeout)` ： 阻塞直到任务完成入队列或者超时


**ScheduleDelayTask**

```
template<class D, class F, class... Args>
auto ScheduleDelayTask(const Context& ctx, D delay_duration, F&& function, Args&&... args) 
        -> std::tuple<StatusCode, std::future<typename std::result_of<F(Args...)>::type>>
```
将一个任务在指定时间点执行。     



**PoolSize**

```
size_t PoolSize();
```
返回线程池大小



**QueueSize**

```
size_t QueueSize();
```
队列中任务数量。


**DelayQueueSize**

```
size_t DelayQueueSize();
```
延迟队列中任务数量。



## 使用

### 使用

**1. 创建线程池**

- 1.1 创建线程池

```
kthreadpool::ThreadPoolSettings settings;
settings.SetKeepaliveTime(idle).SetMaxDelayQueueSize(2).SetMaxQueueSize(3).SetMaxPoolSize(4).SetMinPoolSize(1);
```


- 1.2 使用默认参数创建线程池

```
kthreadpool::ThreadPool tp(settings);
```

或者指定队列来创建线程池

```
std::shared_ptr<BlockingQueue> delayed_queue = std::make_shared<PriorityQueue>(100); // size is 100

kthreadpool::ThreadPool tp(settings, delayed_queue);
```



- 1.3 启动线程池

```
tp.Start();
```



**2. 调度任务**

调度一个延迟任务

```
auto plus_func = [=](int a, int b) -> int {
    return a + b;
};

auto resp = tp.ScheduleDelayTask(Context::WithTimeout(Context(), std::chrono::seconds(1), plus_func, 1, 2);
auto status_code = std::get<0>(resp);
if (status_code != kthreadpool::StatusCode::Ok)
    error

// waiting until the future has a valid result
auto result = std::get<1>(resp).get();
if (result != 3)
    error
```


调度一个普通任务

```
auto plus_func = [=](int a, int b) -> int {
    return a + b;
};

auto resp = tp.ScheduleTask(Context(), plus_func, 1, 2);
auto status_code = std::get<0>(resp);
if (status_code != kthreadpool::StatusCode::Ok)
    error

// waiting until the future has a valid result
auto result = std::get<1>(resp).get();
if (result != 3)
    error
```



**3. 停止线程池**

等待所有任务被执行才返回
```
tp.Stop();
```



### 例子

```
#include <chrono>
#include <iostream>
#include "thread_pool.h"

using namespace kthreadpool;
using namespace std;

int main(int argc, char* argv[])
{
    auto test_func = [](int a) -> int {
        return a * a;
    };

    ThreadPool tp(ThreadPoolSettings().SetMaxQueueSize(2).SetMaxDelayQueueSize(2));
    tp.Start();

    auto resp = tp.ScheduleTask(Context::WithTimeout(Context(), std::chrono::seconds(1)), test_func, 2);
    if (std::get<0>(resp) != StatusCode::Ok)
        return 1;
    cout << "2 * 2 = " << std::get<1>(resp).get() << endl;
    
    auto delay_resp = tp.ScheduleDelayTask(Context(), std::chrono::seconds(1), test_func, 3);
    if (std::get<0>(resp) != StatusCode::Ok)
        return 1;
    cout << "3 * 3 = " << std::get<1>(delay_resp).get() << endl;

    tp.Wait();
    tp.Stop();
}
```





## 构建

线程池完全基于C++11开发，没有其他依赖。

cmake

```
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
add_executable(binary_name thread_pool.cc)
target_link_libraries(binary_name  pthread)
```

make on Linux

```
g++ main.cc thread_pool.cc -o binary_name -std=c++11 -lpthread
```
