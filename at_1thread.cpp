#include <iostream>
#include <thread>
#include <atomic>
#include <functional>

const auto nruns = 10'000'000;
const auto val = 100;

std::atomic<int> atvar(0);
int exptd = 0;
int des = 1;

// Flag to synchronize threads
std::atomic<bool> prepflag(false);

using time_units = std::chrono::nanoseconds;

// Atomic operations:

inline void CAS()
{
    atvar.compare_exchange_weak(exptd, des);
}

inline void SWAP()
{
}

// output: Print elapsed / avg time
void output(double sumtime)
{
    auto avgtime = sumtime / nruns;

    std::cout << "Elapsed time (M) " << double(sumtime) << std::endl;
    std::cout << "Avg time (M) " << avgtime << std::endl;
}

// meas_M: Measure Modified state
// Template variant works slow for -O0
// void meas_M(std::function<void(void)> atop)
void meas_M(void (*atop)(void))
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
    
    output(sumtime);
}

// meas_M: Measure Invalid state
// Template variant works slow for -O0
// void meas_I(std::function<void(void)> atop) 
void meas_I(void (*atop)(void))
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

    output(sumtime);
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
void make_measurements(void (*atop)(void))
{
    meas_M(atop);
    
    std::thread meas_I_thr(meas_I, atop), prep_I_thr(prep_I);

    meas_I_thr.join();
    prep_I_thr.join();
}

int main(int argc, const char *argv[])
{
    make_measurements(CAS);

    return 0;
}
