#include <gtest/gtest.h>
#include <memory>
#include <future>
#include "thread_pool.h"
#include <map>
using namespace kthreadpool;

auto func_sleep = [](std::chrono::nanoseconds ns) {std::this_thread::sleep_for(ns);};
auto func_noop = []()  {};
auto func_multi2 = [](int a) {return a*a;};
auto func_sleep_multi2 = [](std::chrono::nanoseconds ns, int a) {std::this_thread::sleep_for(ns);return a*a;};

class ctpTest : public testing::Test
{
};

class testThreadPool : public ThreadPool
{
public:
    testThreadPool(ThreadPoolSettings set) : ThreadPool(set) {}
    testThreadPool(ThreadPoolSettings set, std::shared_ptr<BlockingQueue> queue) : ThreadPool(set,queue) {}

    bool IsStopped() 
    {
        return PoolSize() == 0;
    }

    std::chrono::system_clock::time_point TimeAddDuration(std::chrono::system_clock::time_point tp, std::chrono::nanoseconds duration)
    {
        return ThreadPool::TimeAddDuration(tp, duration);
    }

    void testWorker()
    {
        auto wid = 100;
        auto ww = std::make_shared<Worker>(this, wid);
        thread_mutex_.lock();
        available_workers_size_++;
        workers_[ww->GetId()] = ww;
        thread_mutex_.unlock();

        ASSERT_EQ(true, ww->IsShutdown());
        ASSERT_EQ(wid, ww->GetId());

        ww->Start();

        std::condition_variable cv;
        std::mutex mutex;
        bool reach = false;
        
        auto fn = [&]() {
            std::unique_lock<std::mutex> lock(mutex);
            reach=true;
            cv.notify_all();
        };
        
        ScheduleTask(Context(), fn);
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() {
                return reach;
            });
        }
        ASSERT_EQ(1, PoolSize());

        ASSERT_EQ(true, ww->Shutdown());
        ww->Wait();

        ASSERT_EQ(0, PoolSize());
        {
            std::unique_lock<std::mutex> lock(thread_mutex_);
            ASSERT_EQ(1, workers_.size());
        }
        GcIdleWorkers();
        {
            std::unique_lock<std::mutex> lock(thread_mutex_);
            ASSERT_EQ(0, workers_.size());
        }
    }
};


TEST_F(ctpTest, TestWorker)
{
    ThreadPoolSettings settings;
    auto idle = std::chrono::seconds(1);
    auto incr = std::chrono::milliseconds(10);
    settings.SetKeepaliveTime(idle).SetMaxPoolSize(4).SetMinPoolSize(0);
    settings.SetScaleoutTime(incr);

    testThreadPool tp(settings, std::make_shared<MixedBlockingQueue>(3, 2));
    tp.Start();

    tp.testWorker();
}


TEST_F(ctpTest, TestSettings)
{
    ThreadPoolSettings settings;
    auto idle = std::chrono::seconds(1);
    auto incr = std::chrono::milliseconds(1);
    settings.SetKeepaliveTime(idle).SetMaxPoolSize(4).SetMinPoolSize(5);
    settings.SetScaleoutTime(incr);

    ASSERT_EQ(idle, settings.KeepaliveTime());
    ASSERT_EQ(incr, settings.ScaleoutTime());
    ASSERT_EQ(4, settings.MaxPoolSize());
    ASSERT_EQ(5, settings.MinPoolSize());
}

TEST_F(ctpTest, TestTimeAddDuration)
{
    ThreadPoolSettings settings;
    testThreadPool tp(settings);

    auto now = std::chrono::system_clock::now();
    auto max = std::chrono::system_clock::time_point::max();
    auto timeout = max - now;

    ASSERT_EQ(max, tp.TimeAddDuration(now, timeout + std::chrono::seconds(1)));
    ASSERT_EQ(max, tp.TimeAddDuration(now, timeout));
    ASSERT_EQ(now + std::chrono::seconds(2), tp.TimeAddDuration(now, std::chrono::seconds(2)));
}

TEST_F(ctpTest, TestThreadNormalCases)
{
    ThreadPoolSettings settings;
    auto idle = std::chrono::seconds(1);
    auto increase_duration = std::chrono::milliseconds(100);
    settings.SetKeepaliveTime(idle).SetMaxPoolSize(2).SetMinPoolSize(1);
    settings.SetScaleoutTime(increase_duration);

    auto delay_dur3 = std::chrono::seconds(3);
    auto sleep_dur1 = std::chrono::seconds(1);

    auto queue = std::make_shared<MixedBlockingQueue>(1, 1);
    testThreadPool tp(settings, queue);
    tp.Start();

    // use case : min thread size
    ASSERT_EQ(1, tp.PoolSize());

    auto ret = tp.ScheduleDelayTask(Context(), delay_dur3, func_noop);
    ASSERT_EQ(StatusCode::Ok, std::get<0>(ret));

    ASSERT_EQ(1, queue->QueueSize());

    // use case : delay queue size
    ASSERT_EQ(1, queue->DelayQueueSize());

    // use case : thread is sleeping(queue is empty), 
    //            schedule a new task and wake the thread up
    auto ret1 = tp.ScheduleTask(Context(), func_sleep, sleep_dur1);   // 1 thread is running
    ASSERT_EQ(StatusCode::Ok, std::get<0>(ret1));
    ASSERT_EQ(1, queue->DelayQueueSize());
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    ASSERT_EQ(0, queue->FifoQueueSize());

    // use case : increase thread
    tp.ScheduleTask(Context(), func_sleep, sleep_dur1);   // 1 thread is running
    ASSERT_EQ(StatusCode::Ok, std::get<0>(ret1));
    tp.ScheduleTask(Context(), func_sleep, sleep_dur1);   // queue size
    ASSERT_EQ(StatusCode::Ok, std::get<0>(ret1));
    ASSERT_EQ(2, tp.PoolSize());
    ASSERT_EQ(1, queue->FifoQueueSize());
    ASSERT_EQ(1, queue->DelayQueueSize());

    // use case : ScheduleTask with timeout
    std::async([&]() {
        ret1 = tp.ScheduleTask(Context::WithTimeout(Context(), sleep_dur1), func_noop);
        ASSERT_EQ(StatusCode::Ok, std::get<0>(ret1));
    });
    ret1 = tp.ScheduleTask(Context::WithTimeout(Context(), increase_duration), func_sleep, sleep_dur1); 
    ASSERT_EQ(StatusCode::Timeout, std::get<0>(ret1));

    // use case : Wait() function
    tp.Stop();

    // use case : descrease idle thread
    std::this_thread::sleep_for(idle + std::chrono::milliseconds(20));
    ASSERT_EQ(0, tp.PoolSize());

    ASSERT_EQ(0, queue->QueueSize());
    ASSERT_EQ(0, queue->DelayQueueSize());
    ASSERT_TRUE(tp.IsStopped());
}


TEST_F(ctpTest, TestThreadStopCases)
{
    ThreadPoolSettings settings;
    auto idle = std::chrono::seconds(1);
    auto increase_duration = std::chrono::milliseconds(100);
    settings.SetKeepaliveTime(idle).SetMaxPoolSize(4).SetMinPoolSize(1);
    settings.SetScaleoutTime(increase_duration);

    auto sleep_dur250 = std::chrono::milliseconds(250);
    auto sleep_dur100 = std::chrono::milliseconds(100);

    auto queue = std::make_shared<MixedBlockingQueue>(4, 4);
    testThreadPool tp(settings, queue);
    tp.Start();

    // use case : min thread size
    ASSERT_EQ(1, tp.PoolSize());

    std::vector<std::tuple<kthreadpool::StatusCode, std::future<int>>> delay_rets;
    for (int i = 0; i < 4; i++)
    {
        auto ret = tp.ScheduleDelayTask(Context(), sleep_dur250, func_multi2, i);
        delay_rets.emplace_back(std::move(ret));
    }
    
    std::vector<std::tuple<kthreadpool::StatusCode, std::future<int>>> fifo_rets;
    for (int i = 0; i < 4; i++)
    {
        auto ret = tp.ScheduleTask(Context(), func_sleep_multi2, sleep_dur100, i);   // 1 thread is running
        fifo_rets.emplace_back(std::move(ret));
    }

    // use case : Wait() function
    tp.Stop();

    //
    for (int i = 0; i < 4 ; i++)
    {
        auto& ret = fifo_rets[i];
        ASSERT_EQ(kthreadpool::StatusCode::Ok, std::get<0>(ret));
        ASSERT_EQ(i*i, std::get<1>(ret).get());
    }
    for (int i = 0; i < 4 ; i++)
    {
        auto& ret = delay_rets[i];
        ASSERT_EQ(kthreadpool::StatusCode::Ok, std::get<0>(ret));
        ASSERT_EQ(i*i, std::get<1>(ret).get());
    }

    ASSERT_EQ(0, tp.PoolSize());

    ASSERT_EQ(0, queue->QueueSize());
    ASSERT_EQ(0, queue->DelayQueueSize());
    ASSERT_TRUE(tp.IsStopped());
}



TEST_F(ctpTest, TestThreadDynamicScale)
{
    ThreadPoolSettings settings;
    auto idle = std::chrono::milliseconds(200);
    auto increase_duration = std::chrono::milliseconds(50);
    settings.SetKeepaliveTime(idle).SetMaxPoolSize(4).SetMinPoolSize(0);
    settings.SetScaleoutTime(increase_duration);

    auto queue = std::make_shared<MixedBlockingQueue>(1, 1);
    testThreadPool tp(settings, queue);
    tp.Start();

    auto second1 = std::chrono::seconds(1);

    auto scale_func = [&](){
        // 4 second(parallel) + 1 second
        for (size_t i = 0; i < 5; i++)
        {
            auto ret1 = tp.ScheduleTask(Context(), func_sleep, second1);
            ASSERT_EQ(StatusCode::Ok, std::get<0>(ret1));
        }
        
        // waiting all threads to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        ASSERT_EQ(4, tp.PoolSize());
        
        std::this_thread::sleep_for(second1 * 2 + idle);

        ASSERT_EQ(0, tp.PoolSize());
    };

    scale_func();
    scale_func();
    scale_func();

    tp.Stop();
    ASSERT_EQ(0, queue->QueueSize());
    ASSERT_EQ(0, queue->DelayQueueSize());
    ASSERT_TRUE(tp.IsStopped());
}


TEST_F(ctpTest, TestThreadValue)
{
    ThreadPoolSettings settings;
    auto idle = std::chrono::milliseconds(200);
    auto increase_duration = std::chrono::milliseconds(50);
    settings.SetKeepaliveTime(idle).SetMaxPoolSize(4).SetMinPoolSize(0);
    settings.SetScaleoutTime(increase_duration);

    auto queue = std::make_shared<MixedBlockingQueue>(1, 1);
    testThreadPool tp(settings, queue);
    tp.Start();

    auto plus_func = [=](int a, int b) -> int {
        std::this_thread::sleep_for(increase_duration);
        return a + b;
    };

    std::vector<int> args;
    std::map<int, std::future<int>> rets;
    for (int i = 0; i < 4; i++)
    {
        args.push_back(i);
        auto ret = tp.ScheduleTask(Context(), plus_func, i, i);
        ASSERT_EQ(StatusCode::Ok,std::get<0>(ret));
        rets[i] = (std::move(std::get<1>(ret)));
    }
    
    for (auto& it : rets)
    {
        int ret = it.second.get();
        ASSERT_EQ(it.first + it.first, ret);
    }

    tp.Stop();
    ASSERT_EQ(0, queue->QueueSize());
    ASSERT_EQ(0, queue->DelayQueueSize());
    ASSERT_TRUE(tp.IsStopped());
}


TEST_F(ctpTest, TestThreadDynamicSetting)
{
    ThreadPoolSettings settings;
    auto idle = std::chrono::milliseconds(200);
    auto increase_duration = std::chrono::milliseconds(10);
    settings.SetKeepaliveTime(idle).SetMaxPoolSize(2).SetMinPoolSize(1);
    settings.SetScaleoutTime(increase_duration);

    auto queue = std::make_shared<MixedBlockingQueue>(1, 1);
    testThreadPool tp(settings, queue);
    tp.Start();

    auto second2 = std::chrono::seconds(2);

    for (int i = 0; i < 3; i++) 
    {
        auto ret1 = tp.ScheduleTask(Context(), func_sleep, second2);
        ASSERT_EQ(StatusCode::Ok, std::get<0>(ret1));
    }

    auto ret1 = tp.ScheduleTask(Context::WithTimeout(Context(), std::chrono::milliseconds(20)), func_sleep, second2);
    ASSERT_EQ(StatusCode::Timeout, std::get<0>(ret1));

    tp.GetSettings().SetMaxPoolSize(3);
    ret1 = tp.ScheduleTask(Context::WithTimeout(Context(), std::chrono::milliseconds(20)), func_sleep, second2);
    ASSERT_EQ(StatusCode::Ok, std::get<0>(ret1));

    tp.Stop();
    ASSERT_EQ(0, queue->QueueSize());
    ASSERT_EQ(0, queue->DelayQueueSize());
    ASSERT_TRUE(tp.IsStopped());
}
