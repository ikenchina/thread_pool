#include <chrono>
#include <iostream>
#include "thread_pool.h"

using namespace kthreadpool;
using namespace std;

int main(int argc, char* argv[])
{
    ThreadPoolSettings settings;
    settings.SetMaxPoolSize(1).SetMinPoolSize(1);
    settings.SetKeepaliveTime(std::chrono::milliseconds(200));
    settings.SetScaleoutTime(std::chrono::milliseconds(50));

    ThreadPool tp(settings);
    tp.Start();
    
    auto second1 = std::chrono::seconds(1);
    auto ctx = Context::WithTimeout(Context(), second1);
    
    auto resp = tp.ScheduleTask(ctx, [](int a) {return a*a;}, 2);
    if (std::get<0>(resp) != StatusCode::Ok)
        return 1;
    
    auto delay_resp = tp.ScheduleDelayTask(Context(), second1, 
                            [](int a, int b) {return a + b;}, 3, 4);
    if (std::get<0>(delay_resp) != StatusCode::Ok)
        return 1;

    cout << "2 * 2 = " << std::get<1>(resp).get() << endl;
    cout << "3 + 4 = " << std::get<1>(delay_resp).get() << endl;

    tp.Stop();
}