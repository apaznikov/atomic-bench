#include <iostream>
#include <thread>
#include <atomic>
#include <functional>
#include <map>

#include "utils.h"

const auto nruns    = 5'000'000;
const auto val      = 100;
const auto nthreads = 4;

std::atomic<int> atvar(0);
int exptd = 0;
int des = 1;
int loaded = 0;

// Sum of avg times and mutex to protect it
struct avgtime_val {
    std::string atop_name;
    std::string MESI_state;
    double time;
};

std::map<std::string, avgtime_val> avgtime_sum;

std::mutex mut;

// Flag to synchronize threads
std::atomic<bool> prepflag(false);

using time_units = std::chrono::nanoseconds;

barrier barr(nthreads);

//
// Atomic operations:
//

inline void CAS()
{
    atvar.compare_exchange_weak(exptd, des);
}

inline void SWAP()
{
    loaded = atvar.exchange(des);
}

inline void FAA()
{
    atvar.fetch_add(1);
}

inline void load()
{
    loaded = atvar.load();
}

inline void store()
{
    atvar.store(des);
}

// output: Print elapsed / avg time
void output(double sumtime, const std::string &atop_name, 
            const std::string &MESI_state, int ithr)
{
    auto avgtime = sumtime / nruns;

    std::lock_guard<std::mutex> lock(mut);

    std::string key = atop_name + MESI_state;

    auto search = avgtime_sum.find(key);
    
    if (search == avgtime_sum.end()) {
        avgtime_val val{atop_name, MESI_state, avgtime};
        std::pair<std::string, avgtime_val> elem(key, val);
        avgtime_sum.insert(elem);
    } else {
        auto avgtime_prev = search->second.time;
        avgtime_val val{atop_name, MESI_state, avgtime_prev + avgtime};
        search->second = val;
    }

    // std::cout << "THREAD " << ithr << ": Elapsed time (" << MESI_state << ")"
    //           << sumtime << std::endl;

    std::cout << "THREAD " << ithr << ": Avg time (" << MESI_state << ") " 
              << avgtime << std::endl;
}

// meas_M: Measure Modified state
// Template variant works slow for -O0
// void meas_M(std::function<void(void)> atop)
void meas_M(void (*atop)(void), const std::string &atop_name, int ithr)
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
    
    output(sumtime, atop_name, "M", ithr);
}

// meas_M: Measure Invalid state
// Template variant works slow for -O0
// void meas_I(std::function<void(void)> atop) 
void meas_I(void (*atop)(void), const std::string &atop_name, int ithr)
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

    output(sumtime, atop_name, "I", ithr);
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
void make_measurements(void (*atop)(void), const std::string &atop_name, 
                       int ithr)
{
    std::cout << "thr " << ithr << std::endl;

    barr.wait();

    meas_M(atop, atop_name, ithr);

    barr.wait();
    
    std::thread meas_I_thr(meas_I, atop, atop_name, ithr), 
                prep_I_thr(prep_I);

    meas_I_thr.join();
    prep_I_thr.join();
}

// output_global: 
void output_global()
{
    std::cout << "TOTAL output:" << std::endl;

    for (auto &elem: avgtime_sum) {
        auto atop_name = elem.second.atop_name;
        auto MESI_state = elem.second.MESI_state;
        auto avgtime = elem.second.time;
        std::cout << atop_name << " " << MESI_state 
                  << " " << avgtime << std::endl;
    }
}

int main(int argc, const char *argv[])
{
    using atop_vec_elem_t = std::pair<std::string, void (*)(void)>;
    std::vector<atop_vec_elem_t> atops{
        {"CAS", CAS}, {"SWAP", SWAP}, {"FAA", FAA}, 
        {"load", load}, {"store", store}};

    for (auto &atop_item: atops) {
        std::string atop_name = atop_item.first;
        void (*atop)(void) = atop_item.second;

        std::vector<std::thread> meas_threads;

        for (auto ithr = 0; ithr < nthreads; ithr++) {
            std::thread thr(make_measurements, atop, atop_name, ithr);
            meas_threads.emplace_back(std::move(thr));
        }

        for (auto &thr: meas_threads) {
            thr.join();
        }

        output_global();
    }

    return 0;
}
