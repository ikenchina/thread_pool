# C++ thread pool



## Introduction

Lightweight, generic, pure C++ thread pool.  

It supports 

- Task : thread pool executes it ASAP
- Delay task : thread pool execute it at a point in time
- Scale out/in pool size dynamically



## Design



### APIs



#### enum

```
enum StatusCode
{
    Ok = 1,             // 
    Timeout = -0x2,     // schedule timeout, 
    Closed = -0x3,      // thread pool is closed
};
```



#### class



##### **Context**

**WithTimeout**

```
static Context WithTimeout(const Context& ctx = Context(), std::chrono::nanoseconds t = std::chrono::nanoseconds::max())
```

Create a new context with timeout.



**WithDeadline**

```
static Context WithDeadline(const Context& ctx = Context(), std::chrono::system_clock::time_point deadline = std::chrono::time_point<std::chrono::system_clock>::max())
```

Create a new context with a deadline.




##### ThreadPoolSettings (configurations)


**SetMinPoolSize**

```
ThreadPoolSettings& SetMinPoolSize(size_t t)
```

Set minimum number of threads, default value is 1.


**SetMaxPoolSize**

```
ThreadPoolSettings& SetMaxPoolSize(size_t t)
```

Set maximum number of threads, default value is the number of CPU cores + 1.


**SetMaxQueueSize**

```
ThreadPoolSettings& SetMaxQueueSize(size_t t)
```

Set max queue size, if reach this value, `ScheduleTask` will be blocked. Default value is 1000.


**SetMaxDelayQueueSize**

```
ThreadPoolSettings& SetMaxDelayQueueSize(size_t t)
```

Set max delay queue size, if reach this value, `ScheduleDelayTask` will be blocked. Default value is 1000.


**SetKeepaliveTime**

```
ThreadPoolSettings& SetKeepaliveTime(std::chrono::nanoseconds idle_duration)
```

Set a maximum duration, if a thread waits a task more than this value, the thread will be destroy. 
Default value is 10 seconds.


**SetScaleoutTime**

```
ThreadPoolSettings& SetScaleoutTime(std::chrono::nanoseconds duration)
```

If queue is full and waits to enqueue more than this duration value, create a new thread when thread number is less `SetMaxPoolSize`.
Default value is 300 milliseconds.





##### BlockingQueue

**Push**

```
virtual StatusCode Push(const Context& ctx, const Task& task) = 0;
```

Enqueue a task to queue.

Enqueue a task to a full queue is blocked until queue is available or Context is timeout.



**Pop**

```
virtual StatusCode Pop(const Context& ctx, Task* t) = 0;
```

Dequeue a task from queue.

Dequeue a task from an empty queue until other threads enqueue a task or Context is timeout.



**Clear**

```
virtual void Clear() = 0;
```

Remove all tasks from queue.





##### BaseBlockingQueue

```
BaseBlockingQueue(std::shared_ptr<Queue> queue = std::make_shared<FifoQueue>())
```

Create a blocking queue.

Default queue is a FifoQueue.





##### MixedBlockingQueue

```
MixedBlockingQueue(size_t fifo_queue_size = 1000, size_t delay_queue_size=1000)
```

Create a mixed BlockingQueue.

MixedBlockingQueue consist of a FifoQueue and a DelayQueue.





##### ThreadPool



**Constructor**

```
ThreadPool(ThreadPoolSettings settings, std::shared_ptr<BlockingQueue> queue = std::make_shared<MixedBlockingQueue>());
```

Create a thread pool.

Parameters:

- settings
- queue :  blocking queue,





**Start**

```
void Start();
```

Initliaze minimum number of threads.



**Stop**

```
void Stop(bool force=false);
```

force

- False : wait thread pool to complete all tasks.
- True : clear all pending tasks and wait to complete ongoing tasks.



**ScheduleTask**

```
template<class F, class... Args>
auto ScheduleTask(const Context& ctx, F&& function, Args&&... args) 
        -> std::tuple<StatusCode, std::future<typename std::result_of<F(Args...)>::type>>
```

Push a task to queue and execute it as soon as possible.

- Context : 
  - `Context()`  : blocks until the task be queued
  - `Context::WithTimeout(Context(), duration_timeout)` : blocks until the task be queued or timeout
  - function 
  - args : variadic function arguments 



**ScheduleDelayTask**

```
template<class D, class F, class... Args>
auto ScheduleDelayTask(const Context& ctx, D delay_duration, F&& function, Args&&... args) 
        -> std::tuple<StatusCode, std::future<typename std::result_of<F(Args...)>::type>>
```

Push a delay task to delay queue and execute it at a point in time.

- Context
- delay_duration : executes the function after delay_duration
  - Function
  - args : variadic function arguments 



**PoolSize**

```
size_t PoolSize();
```

The number of runnable threads.



**QueueSize**

```
size_t QueueSize();
```

How many tasks in queue.



**DelayQueueSize**

```
size_t DelayQueueSize();
```

How many tasks in delay queue.





## Usage



### Usage



**1. create thread pool**

- 1.1 create a settings first

```
kthreadpool::ThreadPoolSettings settings;
settings.SetKeepaliveTime(idle).SetMaxDelayQueueSize(2).SetMaxQueueSize(3).SetMaxPoolSize(4).SetMinPoolSize(1);
```



- 1.2 create thread pool

```
kthreadpool::ThreadPool tp(settings);
```

or create thread pool with a specific queue.

```
std::shared_ptr<BlockingQueue> delayed_queue = std::make_shared<PriorityQueue>(100); // size is 100

kthreadpool::ThreadPool tp(settings, delayed_queue);
```



- 1.3 start (settings.MinPoolSize()) thread

```
tp.Start();
```



**2. schedule task**


schedule a delayed task

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



schedule a task

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



**3. stop thread pool**

wait threads to complete all tasks.

```
tp.Stop();
```



### Example

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





## Build

ThreadPool is platform independent. It's pure C++11.

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
