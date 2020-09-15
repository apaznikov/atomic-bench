//
// broadcast_binomial.cpp: Binomial tree algorithm for RMA broadcast
//
// (C) 2019 Alexey Paznikov <apaznikov@gmail.com>
//

#pragma once

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <thread>
#include <condition_variable>

///////////////////////////////////////////////////////////
//                 Barrier
///////////////////////////////////////////////////////////

class barrier
{
public:
    barrier() {}

    barrier(int count): thread_count(count) {}

    void init(int count)
    {
        std::unique_lock<std::mutex> lk(mut);
        thread_count = count;
        counter = waiting = 0;
    }

    void wait()
    {
        // fence mechanism
        std::unique_lock<std::mutex> lk(mut);
        ++counter;
        ++waiting;

        cv.wait(lk, [&]{return counter >= thread_count;});
        cv.notify_one();
        --waiting;

        if (waiting == 0) {
            // reset barrier
            counter = 0;
        }
    }

private:
    std::mutex mut;
    std::condition_variable cv;

    int counter = 0;
    int waiting = 0;
    int thread_count = 0;
};

///////////////////////////////////////////////////////////
//                 Utils
///////////////////////////////////////////////////////////

// set_affinity: Set affinity for measurement and preparation thread
//               on different cores
void set_affinity(std::thread &meas_thr, std::thread &prep_thr,
                  int meas_cpu, int prep_cpu)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(meas_cpu, &cpuset);

    auto rc = pthread_setaffinity_np(meas_thr.native_handle(),
                                    sizeof(cpu_set_t), &cpuset);

    if (rc != 0) {
        std::cerr << "pthread_setaffinity_np() failed" << std::endl;
        exit(1);
    }

    CPU_ZERO(&cpuset);
    CPU_SET(prep_cpu, &cpuset);

    rc = pthread_setaffinity_np(prep_thr.native_handle(),
                                sizeof(cpu_set_t), &cpuset);

    if (rc != 0) {
        std::cerr << "pthread_setaffinity_np() failed" << std::endl;
        exit(1);
    }
}

// set_affinity: Set affinity for measurement and preparation thread
//               on different cores
void set_affinity(std::thread &meas_thr, int meas_cpu)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(meas_cpu, &cpuset);

    auto rc = pthread_setaffinity_np(meas_thr.native_handle(),
                                    sizeof(cpu_set_t), &cpuset);

    if (rc != 0) {
        std::cerr << "pthread_setaffinity_np() failed" << std::endl;
        exit(1);
    }
}

// set_affinity_by_tid: Set affinity for measurement thread by thread id
void set_affinity_by_tid(std::thread &thr, int tid)
{
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    const auto ncores = std::thread::hardware_concurrency();
    CPU_SET(tid % ncores, &cpuset);

    auto rc = pthread_setaffinity_np(thr.native_handle(),
                                     sizeof(cpu_set_t), &cpuset);

    if (rc != 0) {
        std::cerr << "pthread_setaffinity_np() failed" << std::endl;
        exit(1);
    }
}
