#include <iostream>
#include <vector>
#include <chrono>
#include <thread>
#include <future>
#include <cmath>
#include <fstream>
#include <cstring>
#include <numeric>
#include <algorithm>
#include <fstream>
// Standard high-performance CPU Prime Sieve benchmark
long long run_cpu_subtask(int start, int end) {
    long long count = 0;
#include <numeric>
    // Handle even number corner cases
    if (start <= 2 && end >= 2) {
        count++;
// Standard high-performance CPU Prime Sieve benchmark
    
long long run_cpu_subtask(int start, int end) {
    if (start % 2 == 0) {
    long long count = 0;
    }
    for (int i = start; i <= end; ++i) {
    // Handle even number corner cases
    }
    if (start <= 2 && end >= 2) {
    for (int i = start; i <= end; i += 2) {
        int limit = std::sqrt(i);
        // Optimized condition: j * j <= i (integer division to avoid std::sqrt overhead)
        for (int j = 3; j * j <= i; j += 2) {
    // Ensure we start on an odd number >= 3
                is_prime = false;
    if (start % 2 == 0) {
            }
        start++;
        if (is_prime) {
            count++;
        if (is_prime) count++;
        start = 3;
    return count;
    return count;
    
double benchmark_cpu(int& num_threads) {
    unsigned int threads = std::thread::hardware_concurrency();
    if (threads > 1) {
        threads = threads - 1; // Leave at least 1 core free for FastAPI and UI thread responsiveness
    unsigned int threads = std::thread::hardware_concurrency();
        // Only check odd divisors, starting from 3
    }
        for (int j = 3; j <= limit; j += 2) {

    int total_limit = 20000000; // Search primes up to 20M for solid benchmark scores
    int chunk = total_limit / threads;
    int total_limit = 500000; // Search primes up to 500k
    auto start_time = std::chrono::high_resolution_clock::now();
            }
    std::vector<std::future<long long>> futures;
    auto start_time = std::chrono::high_resolution_clock::now();
    std::vector<std::future<long long>> futures;
        int end = (i == threads - 1) ? total_limit : (i + 1) * chunk;
        futures.push_back(std::async(std::launch::async, run_cpu_subtask, start, end));
    for (unsigned int t = 0; t < threads; ++t) {
        int start = t * chunk;
        int end = (t == threads - 1) ? total_limit : (start + chunk - 1);
        futures.push_back(std::async(std::launch::async, run_cpu_subtask, start, end));
        total_primes += f.get();
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> duration = end_time - start_
    unsigned int threads = std::thread::hardware_concurrency();

    if (threads == 0) threads = 4;
        threads = threads - 1; // Leave at least 1 core free for UI responsiveness and FastAPI server

    auto end_time = std::chrono::high_resolution_
    int total_limit = 500000; // Search primes up to 500k
    int chunk = total_limit / threads;
    num_threads = threads;

    auto start_time = std::chrono::high_resolution_clock::now();
    int total_limit = 500000; // Search primes up to 500k

    int chunk = total_limit / threads;
    for (unsigned int t = 0; t < threads; ++t) {
    auto start_time = std::chrono::hig
// MISSING LINE 89
// MISSING LINE 90
// MISSING LINE 91
// MISSING LINE 92
// MISSING LINE 93
// MISSING LINE 94
// MISSING LINE 95
// MISSING LINE 96
// MISSING LINE 97
// MISSING LINE 98
// MISSING LINE 99
// MISSING LINE 100
// MISSING LINE 101
// MISSING LINE 102
// MISSING LINE 103
// MISSING LINE 104
// MISSING LINE 105
// MISSING LINE 106
// MISSING LINE 107
// MISSING LINE 108
// MISSING LINE 109
// MISSING LINE 110
    outfile.close();

    auto write_end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> write_duration = write_end - write_start;

    write_mbs = ((double)size_bytes / (1024.0 * 1024.0)) / write_duration.count();



    // Sequential Read

    std::vector<char> read_buffer(size_bytes);

    auto read_start = std::chrono::high_resolution_clock::now();

    std::ifstream infile(filename, std::ios::binary);

    if (!infile) {

        read_mbs = 0;

        std::remove(filename.c_str());

        return;

    }

    infile.read(read_buffer.data(), size_bytes);

    infile.close();

    auto read_end = std::chrono::high_resolution_clock::now();

    std::chrono::duration<double> read_duration = read_end - read_start;

    read_mbs = ((double)size_bytes / (1024.0 * 1024.0)) / read_duration.count();

    outfile.close();
    auto write_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> write_duration = write_end - write_start;
    write_mbs = ((double)size_bytes / (1024.0 * 1024.0)) / write_duration.count();


    // Sequential Read
    std::vector<char> read_buffer(size_bytes);
    auto read_start = std::chrono::high_resolution_clock::now();
    std::ifstream infile(filename, std::ios::binary);
    char memory_dummy = 0;

    double mem_bandwidth_gbs = benchmark_memory(memory_dummy);

        std::remove(filename.c_str());
    double disk_write_mbs = 0.0;

    double disk_read_mbs = 0.0;

    benchmark_disk(disk_write_mbs, disk_read_mbs);

    infile.close();
    auto read_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> read_duration = read_end - read_start;
    read_mbs = ((double)size_bytes / (1024.0 * 1024.0)) / read_duration.count();
    std::chrono::duration<double> write_duration = write_end - write_start;
    write_mbs = ((double)size_bytes / (1024.0 * 1024.0)) / write_duration.count();
              << "\"disk_write_mbs\":" << disk_write_mbs << ","

    auto write_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> write_duration = write_end - write_start;
    write_mbs = ((double)size_bytes / (1024.0 * 1024.0)) / write_duration.count();
    std::ifstream infile(filename, std::ios::binary);
    double cpu_score = benchmark_cpu(cpu_threads);
    std::vector<char> read_buffer(size_bytes);
    auto read_start = std::chrono::high_resolution_clock::now();
    double mem_bandwidth_gbs = benchmark_memory(memory_dummy);
    if (!infile) {
    infile.read(read_buffer.data(), size_bytes);
        std::remove(filename.c_str());
    auto read_end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> read_duration = read_end - read_start;
    read_mbs = ((double)size_bytes / (1024.0 * 1024.0)) / read_duration.count();
    std::cout << "{"
              << "\"cpu_score\":" << (long long)cpu_score << ","
    std::chrono::duration<double> read_duration = read_end - read_start;
    read_mbs = ((double)size_bytes / (1024.0 * 1024.0)) / read_duration.count();
              << "\"disk_write_mbs\":" << disk_write_mbs << ","
              << "\"disk_read_mbs\":" << disk_read_mbs << ","
              << "\"opt_check\":" << (int)memory_dummy
    double cpu_score = benchmark_cpu(cpu_threads);
    
    char memory_dummy = 0;
    double mem_bandwidth_gbs = benchmark_memory(memory_dummy);
    double cpu_score = benchmark_cpu(cpu_threads);
    double disk_write_mbs = 0.0;
    double disk_read_mbs = 0.0;
    double mem_bandwidth_gbs = benchmark_memory(memory_dummy);
    
    // Output JSON format directly
    double disk_read_mbs = 0.0;
              << "\"cpu_score\":" << (long long)cpu_score << ","
              << "\"cpu_threads\":" << cpu_threads << ","
              << "\"memory_bandwidth_gbs\":" << mem_bandwidth_gbs << ","
              << "\"disk_write_mbs\":" << disk_write_mbs << ","
              << "\"cpu_score\":" << (long long)cpu_score << ","
              << "\"cpu_threads\":" << cpu_threads << ","
              << "\"memory_bandwidth_gbs\":" << mem_bandwidth_gbs << ","
              << "\"disk_write_mbs\":" << disk_write_mbs << ","
              << "\"disk_read_mbs\":" << disk_read_mbs << ","
              << "\"opt_check\":" << (int)memory_dummy
              << "}" << std::endl;

    return 0;
}