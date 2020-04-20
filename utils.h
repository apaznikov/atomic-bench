//
// broadcast_binomial.cpp: Binomial tree algorithm for RMA broadcast
//
// (C) 2019 Alexey Paznikov <apaznikov@gmail.com>
//

#pragma once

#include <thread>
#include <condition_variable>

class barrier
{
public:
    barrier(int count): thread_count(count)
    {}

    void wait()
    {
        //fence mechanism
        std::unique_lock<std::mutex> lk(mut);
        ++counter;
        ++waiting;

        cv.wait(lk, [&]{return counter >= thread_count;});
        cv.notify_one();
        --waiting;

        if (waiting == 0) {
            //reset barrier
            counter = 0;
        }

        lk.unlock();
    }

private:
    std::mutex mut;
    std::condition_variable cv;

    int counter = 0;
    int waiting = 0;
    int thread_count;
};
