#include <windows.h>
#include <iostream>
#include <thread>
#include "src/search_engine.h"

int main() {
    std::cout << "Starting QuickFinder Stress Test..." << std::endl;
    SearchEngine engine;
    
    // Rapid start/stop loops to test thread lifecycle and zombie threads
    for (int i = 0; i < 50; ++i) {
        engine.StartIndexing();
        Sleep(10); // Let it do some work
        engine.StopIndexing();
        std::cout << "Iteration " << i << " complete. Indexed: " << engine.GetIndexedFileCount() << std::endl;
    }
    
    // Concurrency test: Multiple threads triggering searches while indexing
    engine.StartIndexing();
    std::vector<std::thread> search_threads;
    for (int i = 0; i < 20; ++i) {
        search_threads.emplace_back([&engine]() {
            for (int j = 0; j < 100; ++j) {
                std::vector<SearchResult> results;
                SearchOptions opt;
                engine.Search(L"test", results, opt);
                Sleep(5);
            }
        });
    }
    
    for (auto& t : search_threads) {
        t.join();
    }
    
    engine.StopIndexing();
    std::cout << "Stress Test Complete. Final Indexed: " << engine.GetIndexedFileCount() << std::endl;
    return 0;
}
