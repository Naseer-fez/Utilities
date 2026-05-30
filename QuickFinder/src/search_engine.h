#pragma once
/***********************************************************************
 * QuickFinder - Ultra-Fast Multi-Threaded Search Engine
 * 
 * Architecture:
 *   - FileRecord stores dir_index + filename (no path duplication)
 *   - ThreadPool with work-stealing queue for directory crawling
 *   - Lock-striped concurrent cache for parallel insertion
 *   - Parallel search across cached records
 ***********************************************************************/

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <string>
#include <vector>
#include <deque>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <cstdint>

// =====================================================================
// Configuration Constants
// =====================================================================
namespace Config {
    // Maximum number of files in cache (default ~1M files ≈ 50-100MB RAM)
    constexpr uint32_t MAX_CACHE_FILES      = 1'000'000;
    // Number of crawler threads (reduced to prevent CPU spikes)
    constexpr uint32_t CRAWLER_THREADS      = 4;
    // Number of search threads (reduced to prevent CPU spikes)
    constexpr uint32_t SEARCH_THREADS       = 4;
    // Maximum results to display in the GUI
    constexpr uint32_t MAX_DISPLAY_RESULTS  = 500;
    // Minimum query length to trigger search
    constexpr uint32_t MIN_QUERY_LENGTH     = 2;
}

// =====================================================================
// FileRecord - Compact file entry (no path duplication)
// =====================================================================
struct FileRecord {
    uint32_t     dir_index;   // Index into SearchEngine::directories_
    std::wstring name;        // Filename only (no path separator)
};

// =====================================================================
// SearchResult - Full path + display info
// =====================================================================
struct SearchResult {
    std::wstring full_path;
    std::wstring filename;
};

// =====================================================================
// ThreadSafeQueue - Lock-based concurrent queue for directory tasks
// =====================================================================
class ThreadSafeQueue {
public:
    void push(std::wstring path);
    bool try_pop(std::wstring& out);
    bool empty() const;
    size_t size() const;

private:
    mutable CRITICAL_SECTION cs_;
    std::deque<std::wstring> queue_;
    bool cs_initialized_ = false;

public:
    ThreadSafeQueue();
    ~ThreadSafeQueue();
    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;
};

// =====================================================================
// SearchOptions - Advanced Search Parameters
// =====================================================================
struct SearchOptions {
    enum class MatchType {
        CONTAINS,
        STARTS_WITH,
        ENDS_WITH,
        EXACT
    };

    MatchType match_type     = MatchType::CONTAINS;
    bool      case_sensitive = false;
    bool      search_path    = false;
};

// =====================================================================
// SearchEngine - Core engine managing crawling, caching, and searching
// =====================================================================
class SearchEngine {
public:
    SearchEngine();
    ~SearchEngine();

    // Start background indexing of all fixed drives
    void StartIndexing();

    // Stop indexing
    void StopIndexing();

    // Search cached files for a query with advanced options
    // Returns results up to Config::MAX_DISPLAY_RESULTS
    void Search(const std::wstring& query, std::vector<SearchResult>& results, const SearchOptions& options = {});

    // Check if a path exists and open it in Explorer
    // Returns true if the path was valid and Explorer was launched
    bool OpenInExplorer(const std::wstring& path);

    std::wstring BuildFullPath(uint32_t dir_index, const std::wstring& filename) const;

    // Case-insensitive substring check (optimized)
    static bool ContainsCI(const wchar_t* haystack, size_t haystack_len,
                           const wchar_t* needle,   size_t needle_len);

    // Status getters
    uint32_t GetIndexedFileCount() const { return total_files_.load(std::memory_order_relaxed); }
    bool     IsIndexing()          const { return indexing_active_.load(std::memory_order_relaxed); }
    double   GetLastSearchTimeMs() const { return last_search_time_ms_; }
    uint32_t GetDirectoryCount()   const { return dir_count_.load(std::memory_order_relaxed); }

private:
    // --- Directory storage (shared, append-only) ---
    std::deque<std::wstring>  directories_;
    mutable SRWLOCK           dir_lock_;

    // --- File records (shared, append-only) ---
    std::vector<FileRecord>   files_;
    mutable SRWLOCK           file_lock_;

    // --- Crawling infrastructure ---
    ThreadSafeQueue           work_queue_;
    std::atomic<uint32_t>     total_files_{0};
    std::atomic<uint32_t>     dir_count_{0};
    std::atomic<bool>         indexing_active_{false};
    std::atomic<bool>         stop_requested_{false};
    std::atomic<int>          active_workers_{0};

    // Worker threads
    HANDLE                    threads_[Config::CRAWLER_THREADS];
    uint32_t                  thread_count_ = 0;

    // Event to signal completion
    HANDLE                    completion_event_;

    // --- Search diagnostics ---
    double                    last_search_time_ms_ = 0.0;

    // --- Internal methods ---
    void        EnumerateDrives();
    void        CrawlWorker();
    void        ProcessDirectory(const std::wstring& dir_path, std::vector<FileRecord>& local_batch);
    uint32_t    AddDirectory(const std::wstring& path);
    void        AddFile(uint32_t dir_index, const std::wstring& filename);
    void        AddFileBatch(std::vector<FileRecord>& local_batch);
    
    friend class WordFinderEngine;

    // Static thread entry point
    static DWORD WINAPI CrawlThreadProc(LPVOID param);
};
