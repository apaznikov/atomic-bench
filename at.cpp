//
// at.cpp: Small benchmark for atomic operations
//
// (C) 2020 Alexey Paznikov <apaznikov@gmail.com>
//

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

// #define CONT_MEAS_ENABLE
// #define DELAY_MEAS_ENABLE
// #define MESI_MEAS_ENABLE
#define ARRAY_MEAS_ENABLE

const auto nruns      = 10'000;

const auto nthr_min   = 4;
const auto nthr_max   = 10;
const auto nthr_step  = 4;

const auto delay_min  = 0;
// const auto delay_max  = 10000000;
// const auto delay_step = 500000;
const auto delay_max  = 3000;
const auto delay_step = 10;

const auto stride_min = 0;
const auto stride_max = 100;
const auto stride_step = 20;

// const std::vector<int> delay_nthr{2, 4, 8, 16, 32, 64};
const std::vector<int> delay_nthr{16, 32, 64};

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
// TODO replace with array
// std::array<std::atomic<int>, nthr_max> atarr;

atvar_pad *atarr = nullptr;

var_pad *exptd = nullptr;
var_pad *des = nullptr;
var_pad *des2 = nullptr;
var_pad *loaded = nullptr;
var_pad *val = nullptr;

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

//
// Atomic operations (scalar):
//

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

//
// Atomic operations (vector):
//

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
              << delay << " " << " stride " << stride << atop_name 
              << " MESI state " << MESI_state << ": " << avgtime << std::endl;
}

// TODO combine _shared and _notshared into one

// meas_simple_shared: Measure without specified MESI state
//                     All threads use one shared atomic variable
void meas_simple_shared(void (*atop)(int), const std::string &atop_name, 
                        int nthr, int delay, const std::string &test_type)
{
    decltype(get_time()) start, end;
    double sumtime = 0;

    // All threads access to one 0th atomic variable
    const auto ithr = 0;

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
    
    output(sumtime, atop_name, "X1", nthr, ithr, delay, 0, test_type);
}

// meas_simple_notshared: Measure without specified MESI state
//                        Threads access separate thread-local atomic variables
void meas_simple_notshared(void (*atop)(int), const std::string &atop_name, 
                           int nthr, int ithr, int delay, 
                           const std::string &test_type)
{
    decltype(get_time()) start, end;
    double sumtime = 0;

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
    
    output(sumtime, atop_name, "X2", nthr, ithr, delay, 0, test_type);
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
//                 Measurement functions
///////////////////////////////////////////////////////////

// make_cont_meas: Experiments for contention measurements
//                 Many threads make operations with one atomic variables
//                 (MESI state is undefined)
void make_cont_meas(void (*atop)(int), const std::string &atop_name, 
                    int nthr, int ithr)
{
    barr.wait();

    const auto delay = 0;
    meas_simple_shared(atop, atop_name, nthr, delay, 
                       "contention_shared");

    meas_simple_notshared(atop, atop_name, nthr, ithr, delay,
                          "contention_notshared");
}

// make_cont_meas: Experiments for contention measurements
//                 Many threads make operations with one atomic variables
//                 (MESI state is undefined)
void make_delay_meas(void (*atop)(int), const std::string &atop_name, 
                    int nthr, int ithr, int delay)
{
    barr.wait();

    meas_simple_shared(atop, atop_name, nthr, delay, 
                       "delay_shared");

    // meas_simple_notshared(atop, atop_name, nthr, ithr, delay,
    //                       "delay_notshared");
}

// set_affinity: Set affinity for measurement and preparation thread
//               on different cores
void set_affinity(std::thread &meas_thr, std::thread &prep_thr)
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
void set_affinity(std::thread &meas_thr)
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

    set_affinity(meas_thr, prep_thr);

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

    set_affinity(meas_thr);

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

// meas_arr: Array-based measurements
void meas_arr(void (*atop)(int, int), const std::string &atop_name, 
              int nthr, int ithr, int delay, int stride, 
              const std::string &test_type)
{
    decltype(get_time()) start, end;
    double sumtime = 0;

    if (test_type == "array_shared") {
        // All threads access to one 0th atomic variable
        ithr = 0;
    }

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
    
    if (test_type == "array_shared") 
        output(sumtime, atop_name, "A1", nthr, ithr, delay, stride, test_type);
    else
        output(sumtime, atop_name, "A2", nthr, ithr, delay, stride, test_type);
}

// make_arr_meas: Experiments for array-based throughput measurements
//                For different access patterns
void make_arr_meas(void (*atop)(int, int), const std::string &atop_name, 
                    int nthr, int ithr, int delay, int stride)
{
    barr.wait();

    meas_arr(atop, atop_name, nthr, ithr, delay, stride, "array_shared");

    meas_arr(atop, atop_name, nthr, ithr, delay, stride, "array_notshared");
}

///////////////////////////////////////////////////////////
//                 Utils
///////////////////////////////////////////////////////////

// alloc_mem: Allocate all padded arrays
void alloc_mem(int nthr, int nthr_arr)
{
    // Array of atomic variables with paddings
    atarr = new atvar_pad[nthr];

    // Arrays of common variables with paddings
    exptd = new var_pad[nthr];
    des = new var_pad[nthr];
    des2 = new var_pad[nthr];
    loaded = new var_pad[nthr];
    val = new var_pad[nthr];

    if ((atarr == nullptr) || (exptd == nullptr) || (des == nullptr) ||
        (loaded == nullptr) || (val == nullptr)) {
        std::cerr << "Can't allocate memory" << std::endl;
        exit(1);
    }

    for (auto i = 0; i < nthr; i++) {
        atarr[i].atvar = atvar_def;
        exptd[i].var = exptd_def;
        des[i].var = des_def;
        loaded[i].var = loaded_def;
        val[i].var = val_def;

    }

    for (auto i = 0; i < nthr_arr; i++) {
        for (auto j = 0; j < atbuf_size; j++) {
            atbuf[i][j] = atvar_def;
        }
    }
}

// free_mem: Free all padded arrays
void free_mem()
{
    delete[] atarr;
    delete[] exptd;
    delete[] des;
    delete[] des2;
    delete[] loaded;
    delete[] val;
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

        } else if ((test_type == "array_shared") ||
                   (test_type == "array_notshared")) {

            std::string fname = "data/" + test_type + "-" 
                                + atop_name + "-nthr"  
                                + std::to_string(nthr) + ".dat";

            std::fstream ofile(fname, std::fstream::out | std::fstream::app);

            ofile << stride << "\t" << avgtime << std::endl;

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

    alloc_mem(
        std::max(*std::max_element(delay_nthr.begin(), 
                                   delay_nthr.end()), nthr_max),
        nthr_max);

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
    std::cout << "ARRAY MEASUREMENTS\n";
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
                    std::thread thr(make_arr_meas, atop, atop_name, 
                                    nthr, ithr, delay, stride);
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

    free_mem();

    return 0;
}
