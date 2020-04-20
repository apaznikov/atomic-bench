#include <iostream>
#include <thread>
#include <atomic>
#include <functional>

#include "utils.h"

const auto nruns    = 50'000'000;
const auto val      = 100;
const auto nthreads = 8;

std::atomic<int> atvar(0);
int exptd = 0;
int des = 1;

// Sum of avg times and mutex to protect it
double avgtime_sum = 0;
std::mutex mut;

// Flag to synchronize threads
std::atomic<bool> prepflag(false);

using time_units = std::chrono::nanoseconds;

barrier barr(nthreads);

// Atomic operations:

inline void CAS()
{
    atvar.compare_exchange_weak(exptd, des);
}

inline void SWAP()
{
}

// output: Print elapsed / avg time
void output(double sumtime, std::string MESI_state, int ithr)
{
    auto avgtime = sumtime / nruns;

    std::lock_guard<std::mutex> lock(mut);
    avgtime_sum += avgtime;

    // std::cout << "THREAD " << ithr << ": Elapsed time (" << MESI_state << ") " 
    //           << sumtime << std::endl;

    std::cout << "THREAD " << ithr << ": Avg time (" << MESI_state << ") " 
              << avgtime << std::endl;
}

// meas_M: Measure Modified state
// Template variant works slow for -O0
// void meas_M(std::function<void(void)> atop)
void meas_M(void (*atop)(void), int ithr)
{
    auto get_time = std::chrono::steady_clock::now;
    decltype(get_time()) start, end;
    double sumtime = 0;

    // Write var to set M (Modified) state
    atvar.store(val);

    for (auto i = 0; i < nruns; i++) {
        start = get_time();
        atop();
        end = get_time();

        auto elapsed = std::chrono::duration_cast
             <time_units>(end - start).count();
        sumtime += elapsed;
    }
    
    output(sumtime, "M", ithr);
}

// meas_M: Measure Invalid state
// Template variant works slow for -O0
// void meas_I(std::function<void(void)> atop) 
void meas_I(void (*atop)(void), int ithr)
{
    auto get_time = std::chrono::steady_clock::now;
    decltype(get_time()) start, end;
    double sumtime = 0;

    for (auto i = 0; i < nruns; i++) {
        while (prepflag.load() == false) {}

        start = get_time();
        atop();
        end = get_time();

        auto elapsed = std::chrono::duration_cast
             <time_units>(end - start).count();
        sumtime += elapsed;

        prepflag.store(true);
    }

    output(sumtime, "I", ithr);
}

// prep_I: Set Invalid state
void prep_I()
{
    for (auto i = 0; i < nruns; i++) {
        atvar.store(val);
        prepflag.store(true);
        while (prepflag.load() == false) {}
    }
}

// make_measurements: Perform experiments for one atomic operation
void make_measurements(void (*atop)(void), int ithr)
{
    std::cout << "thr " << ithr << std::endl;

    barr.wait();

    meas_M(atop, ithr);
    
    std::thread meas_I_thr(meas_I, atop, ithr), prep_I_thr(prep_I);

    meas_I_thr.join();
    prep_I_thr.join();
}

int main(int argc, const char *argv[])
{
    std::vector<std::thread> meas_threads;

    for (auto ithr = 0; ithr < nthreads; ithr++) {
        std::thread thr(make_measurements, CAS, ithr);
        meas_threads.emplace_back(std::move(thr));
    }

    for (auto &thr: meas_threads) {
        thr.join();
    }

    return 0;
}
