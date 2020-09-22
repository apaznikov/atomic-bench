//
// at.cpp: Small benchmark for atomic operations
//
// (C) 2020 Alexey Paznikov <apaznikov@gmail.com>
//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <iostream>
#include <thread>
#include <atomic>
#include <map>
#include <fstream>
#include <future>
#include <vector>
#include <random>
#include <algorithm>

#include <unistd.h>

#include "utils.h"

//#define CONT_MEAS_ENABLE
//#define DELAY_MEAS_ENABLE
//#define MESI_MEAS_ENABLE
//#define ARRAY_MEAS_ENABLE
#define BARRIER_MEAS_ENABLE

const auto nruns      = 1'000;

const auto nthr_min   = 4;
const auto nthr_max   = 12;
const auto nthr_step  = 4;

const auto delay_min  = 0;
// const auto delay_max  = 10000000;
// const auto delay_step = 500000;
const auto delay_max  = 3000;
const auto delay_step = 1000;

const auto stride_min = 0;
const auto stride_max = 100;
const auto stride_step = 20;

// const std::vector<int> delay_nthr{2, 4, 8, 16, 32, 64};
const std::array<int, 3> delay_nthr{16, 32, 64};

const auto padding_size = 100'000;

const auto atbuf_size = 10000;

const int atvar_def = 0;
const int exptd_def = 0;
const int des_def = 1;
const int des_def2 = 2;
const int loaded_def = 0;
const int val_def = 0;

// Atomic variables with padding
struct atvar_pad {
    std::atomic<int> atvar = 0;
    int padding[padding_size];
};

// Common variable with padding
struct var_pad {
    int var = 0;
    int padding[padding_size];
};

// Make all variables used within threads thread-local
constexpr int nthr_glob = 
    std::max(*std::max_element(delay_nthr.begin(), 
                               delay_nthr.end()), nthr_max);

//atvar_pad *atarr = nullptr;
std::array<atvar_pad, nthr_glob> atarr;

std::array<var_pad, nthr_glob> exptd;
std::array<var_pad, nthr_glob> des;
std::array<var_pad, nthr_glob> des2;
std::array<var_pad, nthr_glob> loaded;
std::array<var_pad, nthr_glob> val;

std::array<std::array<std::atomic<int>, atbuf_size>, nthr_max> atbuf;

// Sum of avg times and mutex to protect it
struct avgtime_val {
    std::string test_type;
    std::string atop_name;
    std::string MESI_state;
    int nthr;
    int delay;
    int stride;
    double time;
};

std::map<std::string, avgtime_val> avgtime_sum;

// Mutex to protect output
std::mutex mut;

// Flag to synchronize threads
std::atomic<bool> prepflag(false);

// Flag to synchronize preparation and measurement threads
std::atomic<bool> meas_ready(false);
std::atomic<bool> prep_ready(false);

using time_units = std::chrono::nanoseconds;

barrier barr;

// Affinity to bind measurement and prep thread
const auto meas_cpu = 0;
const auto prep_cpu = 2;

auto get_time = std::chrono::steady_clock::now;

///////////////////////////////////////////////////////////
//                 Atomic operations (scalar)
///////////////////////////////////////////////////////////

// CAS: successful CAS
inline void CAS(int ithr)
{
    atarr[ithr].atvar.compare_exchange_weak(exptd[ithr].var, des[ithr].var);
}

// unCAS: unsuccessful CAS
inline void unCAS(int ithr)
{
    atarr[ithr].atvar.compare_exchange_weak(des[ithr].var, des2[ithr].var);
}

inline void SWAP(int ithr)
{
    loaded[ithr].var = atarr[ithr].atvar.exchange(des[ithr].var);
}

inline void FAA(int ithr)
{
    atarr[ithr].atvar.fetch_add(1);
}

inline void load(int ithr)
{
    loaded[ithr].var = atarr[ithr].atvar.load();
}

inline void store(int ithr)
{
    atarr[ithr].atvar.store(des[ithr].var);
}

///////////////////////////////////////////////////////////
//                 Atomic operations (buffer)
///////////////////////////////////////////////////////////

// CAS: successful CAS
inline void CAS_arr(int ithr, int ind)
{
    atbuf[ithr][ind].compare_exchange_weak(exptd[ithr].var, des[ithr].var);
}

// unCAS: unsuccessful CAS
inline void unCAS_arr(int ithr, int ind)
{
    atbuf[ithr][ind].compare_exchange_weak(des[ithr].var, des2[ithr].var);
}

inline void SWAP_arr(int ithr, int ind)
{
    loaded[ithr].var = atbuf[ithr][ind].exchange(des[ithr].var);
}

inline void FAA_arr(int ithr, int ind)
{
    atbuf[ithr][ind].fetch_add(1);
}

inline void load_arr(int ithr, int ind)
{
    loaded[ithr].var = atbuf[ithr][ind].load();
}

inline void store_arr(int ithr, int ind)
{
    atbuf[ithr][ind].store(des[ithr].var);
}

///////////////////////////////////////////////////////////
//                 Atomic operations (barrier)
///////////////////////////////////////////////////////////

// CAS: successful CAS
inline void CAS_barr(int ithr)
{
    atarr[ithr].atvar.compare_exchange_weak(exptd[ithr].var, des[ithr].var,
                                            std::memory_order_relaxed,
                                            std::memory_order_relaxed);
}

// unCAS: unsuccessful CAS
inline void unCAS_barr(int ithr)
{
    atarr[ithr].atvar.compare_exchange_weak(des[ithr].var, des2[ithr].var,
                                            std::memory_order_relaxed,
                                            std::memory_order_relaxed);
}

inline void SWAP_barr(int ithr)
{
    loaded[ithr].var = atarr[ithr].atvar.exchange(des[ithr].var,
                                                  std::memory_order_relaxed);
}

inline void FAA_barr(int ithr)
{
    atarr[ithr].atvar.fetch_add(1, std::memory_order_relaxed);
}

inline void load_barr(int ithr)
{
    loaded[ithr].var = atarr[ithr].atvar.load(std::memory_order_relaxed);
}

inline void store_barr(int ithr)
{
    atarr[ithr].atvar.store(des[ithr].var, std::memory_order_relaxed);
}

///////////////////////////////////////////////////////////

std::random_device rd;
std::mt19937 gen(rd());

// dodelay: Sleep for normally-distributed timeout
inline void dodelay(int delay)
{
    std::normal_distribution<> norm_dist{double(delay), double(delay / 10)};
    const auto timeout = std::lround(norm_dist(gen));
    std::this_thread::sleep_for(std::chrono::nanoseconds(timeout));

    // usleep(timeout);
}

// output: Print elapsed / avg time
void output(double sumtime, const std::string &atop_name, 
            const std::string &MESI_state, int nthr, int ithr,
            int delay, int stride, const std::string &test_type)
{
    auto avgtime = sumtime / nruns;

    std::lock_guard<std::mutex> lock(mut);

    std::string key = std::to_string(nthr) + atop_name + MESI_state + 
                      std::to_string(delay) + std::to_string(stride);

    auto search = avgtime_sum.find(key);
    
    if (search == avgtime_sum.end()) {
        avgtime_val val{test_type, atop_name, MESI_state, nthr, 
                        delay, stride, avgtime};
        std::pair<std::string, avgtime_val> elem(key, val);
        avgtime_sum.insert(elem);
    } else {
        auto avgtime_prev = search->second.time;
        avgtime_val val{test_type, atop_name, MESI_state, nthr, 
                        delay, stride, avgtime_prev + avgtime};
        search->second = val;
    }

    std::cout << "nthr " << nthr << " ithr " << ithr << " delay " 
              << delay << " " << " stride " << stride << " " << atop_name 
              << " MESI state " << MESI_state << ": " << avgtime << std::endl;
}

// TODO combine _shared and _notshared into one

// meas_simple: Measure without specified MESI state
void meas_simple(void (*atop)(int), const std::string &atop_name, 
                 int nthr, int ithr, int delay, const std::string &test_type)
{
    decltype(get_time()) start, end;
    double sumtime = 0;

    if ((test_type == "delay_shared") || (test_type == "contention_shared")) {
        // All threads access to one 0th atomic variable
        ithr = 0;
    }

    for (auto i = 0; i < nruns; i++) {
        start = get_time();
        atop(ithr);
        end = get_time();

        // Restore atomic variable
        atarr[ithr].atvar = atvar_def;

        auto elapsed = std::chrono::duration_cast
             <time_units>(end - start).count();
        sumtime += elapsed;

        // Strange, but calling a function (instead for-loop) decreases 
        // the latency of atomic operations, making it equal for all delays
        // dodelay(delay);

        // Loop making a delay
        for (auto j = 0; j < delay; j++);
    }
    
    const auto stride = 0;

    if (test_type == "delay_shared") 
        output(sumtime, atop_name, "DS", nthr, ithr, delay, stride, test_type);
    else if (test_type == "delay_notshared") 
        output(sumtime, atop_name, "DN", nthr, ithr, delay, stride, test_type);
    else if (test_type == "contention_shared") 
        output(sumtime, atop_name, "CS", nthr, ithr, delay, stride, test_type);
    else if (test_type == "contention_notshared") 
        output(sumtime, atop_name, "CN", nthr, ithr, delay, stride, test_type);
}

///////////////////////////////////////////////////////////
//                 State M (Modified)
///////////////////////////////////////////////////////////

// meas_M: Measure Modified state
void meas_M(void (*atop)(int), const std::string &atop_name, 
            const std::string &test_type,
            std::future<void> &affin_ready_fut)
{
    affin_ready_fut.wait();

    decltype(get_time()) start, end;
    double sumtime = 0;

    const auto ithr = 0;

    for (auto i = 0; i < nruns; i++) {
        // Write var to set M (Modified) state
        atarr[ithr].atvar.store(val[ithr].var);

        start = get_time();
        atop(ithr);
        end = get_time();
        
        // Restore atomic variable
        atarr[ithr].atvar = atvar_def;

        auto elapsed = std::chrono::duration_cast
             <time_units>(end - start).count();
        sumtime += elapsed;
    }
    
    output(sumtime, atop_name, "M", 1, ithr, 0, 0, test_type);
}

///////////////////////////////////////////////////////////
//                 State E (Exclusive)
///////////////////////////////////////////////////////////

// meas_E: Measure Modified state
void meas_E(void (*atop)(int), const std::string &atop_name, 
            const std::string &test_type,
            std::shared_future<void> affin_ready_fut)
{
    affin_ready_fut.wait();

    decltype(get_time()) start, end;
    double sumtime = 0;

    const auto ithr = 0;

    for (auto i = 0; i < nruns; i++) {
        // Send a signal to prep_E
        meas_ready = true;

        // Wait until prep_E will invalidate cache-line
        while (prep_ready == false) {}

        // Unset flag for reuse
        prep_ready = false;
        
        // Read var to set E (Exclusive) state
        loaded[ithr].var = atarr[ithr].atvar.load();

        start = get_time();
        atop(ithr);
        end = get_time();
        
        // Restore atomic variable
        atarr[ithr].atvar = atvar_def;

        auto elapsed = std::chrono::duration_cast
             <time_units>(end - start).count();
        sumtime += elapsed;
    }
    
    output(sumtime, atop_name, "E", 1, ithr, 0, 0, test_type);
}

// prep_E: Set Invalid state
void prep_E(std::shared_future<void> affin_ready_fut)
{
    affin_ready_fut.wait();

    const auto ithr = 0;

    for (auto i = 0; i < nruns; i++) {
        // Wait while meas_E will be ready
        while (meas_ready == false) {}

        // Unset meas flag for reuse
        meas_ready = false;
        
        // Invalidate cache-line
        atarr[ithr].atvar.store(val[ithr].var);

        // Signal to meas_E
        prep_ready.store(true);
    }
}

///////////////////////////////////////////////////////////
//                 State I (Invalid)
///////////////////////////////////////////////////////////

// meas_I: Measure Invalid state
void meas_I(void (*atop)(int), const std::string &atop_name, 
            const std::string &test_type, 
            std::shared_future<void> affin_ready_fut)
{
    affin_ready_fut.wait();

    decltype(get_time()) start, end;
    double sumtime = 0;

    const auto ithr = 0;

    for (auto i = 0; i < nruns; i++) {
        // Send a signal to prep_I
        meas_ready = true;

        // Wait for prep_I thread
        while (prep_ready == false) {}

        // Unset flag for reuse
        prep_ready = false;

        start = get_time();
        atop(ithr);
        end = get_time();

        // Restore atomic variable
        atarr[ithr].atvar = atvar_def;

        auto elapsed = std::chrono::duration_cast
             <time_units>(end - start).count();
        sumtime += elapsed;
    }

    output(sumtime, atop_name, "I", 1, ithr, 0, 0, test_type);
}

// prep_I: Set Invalid state
void prep_I(std::shared_future<void> affin_ready_fut)
{
    affin_ready_fut.wait();

    const auto ithr = 0;

    for (auto i = 0; i < nruns; i++) {
        // Wait until meas_I will send a signal
        while (meas_ready == false) {}

        // Unset flag for reuse
        meas_ready = false;

        // Invalidate cache-line
        atarr[ithr].atvar.store(val[ithr].var);

        // Send a signal to meas_I
        prep_ready = true;
    }
}

///////////////////////////////////////////////////////////
//                 State S (Shared)
///////////////////////////////////////////////////////////

// meas_S: Measure Shared state
void meas_S(void (*atop)(int), const std::string &atop_name, 
            const std::string &test_type, 
            std::shared_future<void> affin_ready_fut)
{
    affin_ready_fut.wait();

    decltype(get_time()) start, end;
    double sumtime = 0;

    const auto ithr = 0;

    for (auto i = 0; i < nruns; i++) {
        // Send a signal to prep_E
        meas_ready = true;

        // Wait until prep_E will invalidate cache-line
        while (prep_ready == false) {}

        // Unset flag for reuse
        prep_ready = false;

        // Read var to set S (Shared) state
        loaded[ithr].var = atarr[ithr].atvar.load();

        start = get_time();
        atop(ithr);
        end = get_time();

        // Restore atomic variable
        atarr[ithr].atvar = atvar_def;

        auto elapsed = std::chrono::duration_cast
             <time_units>(end - start).count();
        sumtime += elapsed;

        prepflag.store(true);
    }

    output(sumtime, atop_name, "S", 1, ithr, 0, 0, test_type);
}

// prep_S: Set Shared state
void prep_S(std::shared_future<void> affin_ready_fut)
{
    affin_ready_fut.wait();

    // Measurement thread index
    const auto ithr = 0;

    // Preparation thread index
    const auto prep_ithr = 1;

    for (auto i = 0; i < nruns; i++) {
        // Wait while meas_S will be ready
        while (meas_ready == false) {}

        // Unset meas flag for reuse
        meas_ready = false;

        // Read to set E state
        loaded[prep_ithr].var = atarr[ithr].atvar.load();

        // Signal to meas_E
        prep_ready.store(true);
    }
}

///////////////////////////////////////////////////////////
//                 Contention functions
///////////////////////////////////////////////////////////

// make_cont_meas: Experiments for contention measurements
//                 Many threads make operations with one atomic variables
//                 (MESI state is undefined)
void make_cont_meas(void (*atop)(int), const std::string &atop_name, 
                    int nthr, int ithr)
{
    barr.wait();

    const auto delay = 0;
    meas_simple(atop, atop_name, nthr, ithr, delay, 
                "contention_shared");

    meas_simple(atop, atop_name, nthr, ithr, delay,
                "contention_notshared");
}

///////////////////////////////////////////////////////////
//                 Delay measurement
///////////////////////////////////////////////////////////

// make_cont_meas: Experiments for contention measurements
//                 Many threads make operations with one atomic variables
//                 (MESI state is undefined)
void make_delay_meas(void (*atop)(int), const std::string &atop_name, 
                    int nthr, int ithr, int delay)
{
    barr.wait();

    meas_simple(atop, atop_name, nthr, ithr, delay, 
                "delay_shared");

    // meas_simple_notshared(atop, atop_name, nthr, ithr, delay,
    //                       "delay_notshared");
}


///////////////////////////////////////////////////////////
//                 MESI measurements
///////////////////////////////////////////////////////////

// MESI_meas_prep: Make MESI measurement for specified state
//                 by means of measurement and preparation threads
//                 (only for E, S and I states)
void MESI_do_meas(void (*atop)(int), const std::string &atop_name,
                    std::function<void(void (*)(int), 
                                       const std::string&,
                                       const std::string&, 
                                       std::shared_future<void>)> meas,
                    std::function<void(std::shared_future<void>)> prep)
{
    std::promise<void> affin_ready_promise;
    std::shared_future<void> affin_ready_fut(affin_ready_promise.get_future());

    std::thread meas_thr(meas, atop, atop_name, "MESI", affin_ready_fut), 
                prep_thr(prep, affin_ready_fut);

    set_affinity(meas_thr, prep_thr, meas_cpu, prep_cpu);

    affin_ready_promise.set_value();

    meas_thr.join();
    prep_thr.join();
}

// MESI_M_meas_prep: Make MESI measurement for M (Modified) state
void MESI_M_do_meas(void (*atop)(int), const std::string &atop_name)
{
    std::promise<void> affin_ready_promise;
    std::future<void> affin_ready_fut(affin_ready_promise.get_future());

    std::thread meas_thr(meas_M, atop, atop_name, "MESI", 
                         std::ref(affin_ready_fut));

    set_affinity(meas_thr, meas_cpu);

    affin_ready_promise.set_value();

    meas_thr.join();
}

// make_MESI_meas: Experiments for different MESI states
void make_MESI_meas(void (*atop)(int), const std::string &atop_name)
{
    MESI_do_meas(atop, atop_name, meas_I, prep_I);

    MESI_do_meas(atop, atop_name, meas_E, prep_E);

    MESI_do_meas(atop, atop_name, meas_S, prep_S);

    MESI_M_do_meas(atop, atop_name);
}

///////////////////////////////////////////////////////////
//                 Array-based measurements
///////////////////////////////////////////////////////////

// meas_buf: Array-based measurements
void meas_buf(void (*atop)(int, int), const std::string &atop_name, 
              int nthr, int ithr, int delay, int stride, 
              const std::string &test_type)
{
    decltype(get_time()) start, end;
    double sumtime = 0;

    if (stride == 0)
        stride = 1;

    // All threads access to one 0th atomic variable
    if (test_type == "buf_shared") 
        ithr = 0;

    auto ind = 0;

    for (auto i = 0; i < nruns; i++) {
        start = get_time();
        atop(ithr, ind);
        end = get_time();

        ind = (ind + stride) % atbuf_size;

        // Restore atomic variable
        atarr[ithr].atvar = atvar_def;

        auto elapsed = std::chrono::duration_cast
             <time_units>(end - start).count();
        sumtime += elapsed;

        // Loop-based delay
        for (auto j = 0; j < delay; j++);
    }
    
    if (test_type == "buf_shared") 
        output(sumtime, atop_name, "A1", nthr, ithr, delay, stride, test_type);
    else
        output(sumtime, atop_name, "A2", nthr, ithr, delay, stride, test_type);
}

// make_buf_meas: Experiments for array-based throughput measurements
//                For different access patterns
void make_buf_meas(void (*atop)(int, int), const std::string &atop_name, 
                    int nthr, int ithr, int delay, int stride)
{
    barr.wait();

    meas_buf(atop, atop_name, nthr, ithr, delay, stride, "buf_shared");

    meas_buf(atop, atop_name, nthr, ithr, delay, stride, "buf_notshared");
}

///////////////////////////////////////////////////////////
//                 Barrier measurements
///////////////////////////////////////////////////////////

// meas_barr: Measure barrier impact
void meas_barr(void (*atop1)(int), void (*atop2)(int), 
               const std::string &atop_names,
               int nthr, int ithr, const std::string &test_type)
{
    decltype(get_time()) start, end;
    double sumtime = 0;

    if (test_type == "barr_shared") {
        // All threads access to one 0th atomic variable
        ithr = 0;
    }

    for (auto i = 0; i < nruns; i++) {
        start = get_time();
        atop1(ithr);
        asm volatile("mfence" ::: "memory");
        asm volatile("" ::: "memory");
        atop2(ithr);
        end = get_time();

        // Restore atomic variable
        atarr[ithr].atvar = atvar_def;

        auto elapsed = std::chrono::duration_cast
             <time_units>(end - start).count();
        sumtime += elapsed;
    }
    
    const auto stride = 0;
    const auto delay = 0;

    if (test_type == "barr_shared") 
       output(sumtime, atop_names, "YBS", nthr, ithr, delay, stride, test_type);
    else if (test_type == "barr_notshared") 
       output(sumtime, atop_names, "YBN", nthr, ithr, delay, stride, test_type);
}

// meas_nobarr: Measure barrier impact
void meas_nobarr(void (*atop1)(int), void (*atop2)(int), 
                 const std::string &atop_names,
                 int nthr, int ithr, const std::string &test_type)
{
    decltype(get_time()) start, end;
    double sumtime = 0;

    if (test_type == "nobarr_shared") {
        // All threads access to one 0th atomic variable
        ithr = 0;
    }

    for (auto i = 0; i < nruns; i++) {
        start = get_time();
        atop1(ithr);
        asm volatile("" ::: "memory");
        atop2(ithr);
        end = get_time();

        // Restore atomic variable
        atarr[ithr].atvar = atvar_def;

        auto elapsed = std::chrono::duration_cast
             <time_units>(end - start).count();
        sumtime += elapsed;
    }
    
    const auto stride = 0;
    const auto delay = 0;

    if (test_type == "nobarr_shared") 
       output(sumtime, atop_names, "NBS", nthr, ithr, delay, stride, test_type);
    else if (test_type == "nobarr_notshared") 
       output(sumtime, atop_names, "NBN", nthr, ithr, delay, stride, test_type);
}

// make_barr_meas: Experiments for barrier measurements
void make_barr_meas(void (*atop1)(int), void (*atop2)(int), 
                    const std::string &atop_name1, 
                    const std::string &atop_name2, 
                    int nthr, int ithr)
{
    barr.wait();

    const auto atop_names = atop_name1 + ", " + atop_name2;

    meas_barr(atop1, atop2, atop_names, nthr, ithr, "barr_shared");

    meas_barr(atop1, atop2, atop_names, nthr, ithr, "barr_notshared");

    meas_nobarr(atop1, atop2, atop_names, nthr, ithr, "nobarr_shared");

    meas_nobarr(atop1, atop2, atop_names, nthr, ithr, "nobarr_notshared");
}

///////////////////////////////////////////////////////////
//                 Init, output
///////////////////////////////////////////////////////////

// init_data: Initialize test arrays and buffers
void init_data()
{
    for (auto i = 0; i < nthr_glob; i++) {
        atarr[i].atvar = atvar_def;
        exptd[i].var = exptd_def;
        des[i].var = des_def;
        loaded[i].var = loaded_def;
        val[i].var = val_def;

    }

    for (auto i = 0u; i < atbuf.size(); i++) {
        std::fill(std::begin(atbuf[i]), std::end(atbuf[i]), atvar_def);
    }
}

// output_global: 
void output_global()
{
    std::cout << "=====================================" << std::endl;
    std::cout << "TOTAL output:" << std::endl;
    std::cout << "=====================================" << std::endl;

    for (auto &elem: avgtime_sum) {
        const auto test_type = elem.second.test_type;
        const auto atop_name = elem.second.atop_name;
        const auto MESI_state = elem.second.MESI_state;
        const auto avgtime = elem.second.time / elem.second.nthr;
        const auto nthr = elem.second.nthr;
        const auto delay = elem.second.delay;
        const auto stride = elem.second.stride;

        std::cout << "NTHR " << nthr << " " << atop_name << " " 
                  << MESI_state << " " << avgtime << std::endl;

        if ((test_type == "contention_shared") || 
            (test_type == "contention_notshared")) {

            std::string fname = "data/" + test_type + "-" 
                                     + atop_name + ".dat";

            std::ifstream check_file(fname);
            std::fstream ofile(fname, std::fstream::out | std::fstream::app);

            if (!check_file.good()) {
                ofile << "nthr\ttime\n";
            }

            ofile << nthr << "\t" << avgtime << std::endl;

            check_file.close();
            ofile.close();
        } else if ((test_type == "delay_shared") ||
                   (test_type == "delay_notshared")) {

            std::string fname = "data/" + test_type + "-" 
                                + atop_name + "-nthr"  
                                + std::to_string(nthr) + ".dat";

            std::ifstream check_file(fname);
            std::fstream ofile(fname, std::fstream::out | std::fstream::app);

            if (!check_file.good()) {
                ofile << "delay\ttime\n";
            }

            ofile << delay << "\t" << avgtime << std::endl;

            check_file.close();
            ofile.close();
        } else if (test_type == "MESI") {

            std::string fname = "data/" + test_type + "-"
                                + MESI_state + ".dat";

            std::fstream ofile(fname, std::fstream::out | std::fstream::app);

            ofile << atop_name << "\t" << avgtime << std::endl;

            ofile.close();

        } else if ((test_type == "buf_shared") ||
                   (test_type == "buf_notshared")) {

            std::string fname = "data/" + test_type + "-" 
                                + atop_name + "-nthr"  
                                + std::to_string(nthr) + ".dat";

            std::fstream ofile(fname, std::fstream::out | std::fstream::app);

            ofile << stride << "\t" << avgtime << std::endl;

            ofile.close();
        } else if ((test_type == "barr_shared") ||
                   (test_type == "barr_notshared") ||
                   (test_type == "nobarr_shared") ||
                   (test_type == "nobarr_notshared")) {

            std::string fname = "data/" + test_type +
                                "-nthr" + std::to_string(nthr) + ".dat";

            std::fstream ofile(fname, std::fstream::out | std::fstream::app);

            ofile << atop_name << "\t" << avgtime << std::endl;

            ofile.close();
        }
    }

    avgtime_sum.clear();
}

int main(int argc, const char *argv[])
{
    std::cout << "cores: " << std::thread::hardware_concurrency() << std::endl;

    using atop_vec_elem_t = std::pair<std::string, void (*)(int)>;
    std::vector<atop_vec_elem_t> atops{
        {"CAS", CAS}, {"unCAS", unCAS}, {"SWAP", SWAP}, 
        {"FAA", FAA}, {"load", load}, {"store", store}};

    init_data();

#ifdef CONT_MEAS_ENABLE
    // Contention measurements for different thread number
    std::cout << "-------------------------------------" << std::endl;
    std::cout << "CONTENTION MEASUREMENTS\n";
    std::cout << "-------------------------------------" << std::endl;

    for (auto nthr = nthr_min; nthr <= nthr_max; nthr += nthr_step) {

        std::cout << "Number of threads: " << nthr << std::endl;
        barr.init(nthr);

        for (auto &atop_item: atops) {
            std::string atop_name = atop_item.first;
            void (*atop)(int) = atop_item.second;

            std::cout << atop_name << std::endl;

            std::vector<std::thread> meas_threads;

            // Launch measurement threads
            for (auto ithr = 0; ithr < nthr; ithr++) {
                std::thread thr(make_cont_meas, atop, atop_name, 
                                nthr, ithr);
                set_affinity_by_tid(thr, ithr);
                meas_threads.emplace_back(std::move(thr));
            }

            for (auto &thr: meas_threads) {
                thr.join();
            }
        }

        output_global();
    }
#endif

#ifdef DELAY_MEAS_ENABLE
    // Delay measurements for different delays between atomic operations
    std::cout << "-------------------------------------" << std::endl;
    std::cout << "DELAY MEASUREMENTS\n";
    std::cout << "-------------------------------------" << std::endl;
    for (auto delay = delay_min; delay <= delay_max; delay += delay_step) {
        for (auto nthr: delay_nthr) {

            std::cout << "Number of threads: " << nthr << std::endl;
            barr.init(nthr);

            for (auto &atop_item: atops) {
                std::string atop_name = atop_item.first;
                void (*atop)(int) = atop_item.second;

                std::cout << atop_name << std::endl;

                std::vector<std::thread> meas_threads;

                // Launch measurement threads
                for (auto ithr = 0; ithr < nthr; ithr++) {
                    std::thread thr(make_delay_meas, atop, atop_name, 
                                    nthr, ithr, delay);
                    set_affinity_by_tid(thr, ithr);
                    meas_threads.emplace_back(std::move(thr));
                }

                for (auto &thr: meas_threads) {
                    thr.join();
                }
            }

            output_global();
        }
    }
#endif

#ifdef MESI_MEAS_ENABLE
    // Measurements for different MESI state (number of threads is 1)
    std::cout << "-------------------------------------" << std::endl;
    std::cout << "MESI MEASUREMENTS\n";
    std::cout << "-------------------------------------" << std::endl;

    for (auto &atop_item: atops) {
        std::string atop_name = atop_item.first;
        void (*atop)(int) = atop_item.second;

        std::cout << atop_name << std::endl;

        make_MESI_meas(atop, atop_name);

        output_global();
    }
#endif

#ifdef ARRAY_MEAS_ENABLE
    using atop_arr_vec_elem_t = std::pair<std::string, void (*)(int, int)>;
    std::vector<atop_arr_vec_elem_t> atops_arr{
        {"CAS", CAS_arr}, {"unCAS", unCAS_arr}, {"SWAP", SWAP_arr}, 
        {"FAA", FAA_arr}, {"load", load_arr}, {"store", store_arr}};

    // Array-based measurements for different access patterns
    std::cout << "-------------------------------------" << std::endl;
    std::cout << "BUFFER (ARRAY) MEASUREMENTS\n";
    std::cout << "-------------------------------------" << std::endl;

    for (auto nthr = nthr_min; nthr <= nthr_max; nthr += nthr_step) {

        std::cout << "Number of threads: " << nthr << std::endl;

        for (auto stride = stride_min; 
             stride <= stride_max; stride += stride_step) {

            barr.init(nthr);

            for (auto &atop_item: atops_arr) {
                std::string atop_name = atop_item.first;
                void (*atop)(int, int) = atop_item.second;

                std::cout << atop_name << std::endl;

                std::vector<std::thread> meas_threads;

                // Launch measurement threads
                for (auto ithr = 0; ithr < nthr; ithr++) {
                    const auto delay = 0;
                    std::thread thr(make_buf_meas, atop, atop_name, 
                                    nthr, ithr, delay, stride);
                    set_affinity_by_tid(thr, ithr);
                    meas_threads.emplace_back(std::move(thr));
                }

                for (auto &thr: meas_threads) {
                    thr.join();
                }
            }

            output_global();
        }
    }
#endif

#ifdef BARRIER_MEAS_ENABLE
    std::vector<atop_vec_elem_t> atops_barr_op1{
        {"CAS", CAS_barr}, {"SWAP", SWAP_barr}, 
        {"FAA", FAA_barr}, {"store", store_barr}};

    std::vector<atop_vec_elem_t> atops_barr_op2{
        {"CAS", CAS_barr}, {"SWAP", SWAP_barr}, 
        {"FAA", FAA_barr}, {"load", load_barr}};

    std::cout << "-------------------------------------" << std::endl;
    std::cout << "BARRIER (RELAXATION) MEASUREMENTS\n";
    std::cout << "-------------------------------------" << std::endl;

    for (auto nthr = nthr_min; nthr <= nthr_max; nthr += nthr_step) {

        std::cout << "Number of threads: " << nthr << std::endl;
        barr.init(nthr);

        for (auto &atop_item1: atops_barr_op1) {
            for (auto &atop_item2: atops_barr_op2) {

                std::string atop_name1 = atop_item1.first;
                void (*atop1)(int) = atop_item1.second;

                std::string atop_name2 = atop_item2.first;
                void (*atop2)(int) = atop_item2.second;

                std::cout << atop_name1 << " >> " << atop_name2 << std::endl;

                std::vector<std::thread> meas_threads;

                // Launch measurement threads
                for (auto ithr = 0; ithr < nthr; ithr++) {
                    std::thread thr(make_barr_meas, atop1, atop2, 
                                    atop_name1, atop_name2,
                                    nthr, ithr);
                    set_affinity_by_tid(thr, ithr);
                    meas_threads.emplace_back(std::move(thr));
                }

                for (auto &thr: meas_threads) {
                    thr.join();
                }
            }
        }

        output_global();
    }
#endif

    return 0;
}
