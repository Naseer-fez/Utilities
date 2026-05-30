/***********************************************************************
 * FileFinder - Search Engine Implementation
 * 
 * Ultra-optimized multi-threaded file crawler and search engine.
 * Uses raw Win32 FindFirstFileW/FindNextFileW for maximum I/O speed.
 ***********************************************************************/

#include "search_engine.h"
#include <shlwapi.h>
#include <shellapi.h>
#include <cwctype>

#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

// =====================================================================
// ThreadSafeQueue Implementation
// =====================================================================

ThreadSafeQueue::ThreadSafeQueue() {
    InitializeCriticalSectionAndSpinCount(&cs_, 4000);
    cs_initialized_ = true;
}

ThreadSafeQueue::~ThreadSafeQueue() {
    if (cs_initialized_) {
        DeleteCriticalSection(&cs_);
    }
}

void ThreadSafeQueue::push(std::wstring path) {
    EnterCriticalSection(&cs_);
    queue_.push_back(std::move(path));
    LeaveCriticalSection(&cs_);
}

bool ThreadSafeQueue::try_pop(std::wstring& out) {
    EnterCriticalSection(&cs_);
    if (queue_.empty()) {
        LeaveCriticalSection(&cs_);
        return false;
    }
    out = std::move(queue_.front());
    queue_.pop_front();
    LeaveCriticalSection(&cs_);
    return true;
}

bool ThreadSafeQueue::empty() const {
    EnterCriticalSection(&cs_);
    bool result = queue_.empty();
    LeaveCriticalSection(&cs_);
    return result;
}

size_t ThreadSafeQueue::size() const {
    EnterCriticalSection(&cs_);
    size_t result = queue_.size();
    LeaveCriticalSection(&cs_);
    return result;
}

// =====================================================================
// SearchEngine Implementation
// =====================================================================

SearchEngine::SearchEngine() {
    InitializeSRWLock(&dir_lock_);
    InitializeSRWLock(&file_lock_);
    completion_event_ = CreateEventW(NULL, TRUE, FALSE, NULL);

    // Pre-allocate storage for performance (directories_ deque does not need reservation)
    files_.reserve(Config::MAX_CACHE_FILES);

    for (uint32_t i = 0; i < Config::CRAWLER_THREADS; ++i) {
        threads_[i] = NULL;
    }
}

SearchEngine::~SearchEngine() {
    StopIndexing();
    if (completion_event_) CloseHandle(completion_event_);
}

// ---------------------------------------------------------------------
// Drive Enumeration
// ---------------------------------------------------------------------
void SearchEngine::EnumerateDrives() {
    DWORD drives = GetLogicalDrives();
    wchar_t root[] = L"A:\\";

    for (int i = 0; i < 26; ++i) {
        if (drives & (1 << i)) {
            root[0] = L'A' + i;
            UINT type = GetDriveTypeW(root);
            // Only index fixed drives and removable (USB) drives
            if (type == DRIVE_FIXED || type == DRIVE_REMOVABLE) {
                work_queue_.push(std::wstring(root));
            }
        }
    }
}

// ---------------------------------------------------------------------
// Thread Entry Point (static)
// ---------------------------------------------------------------------
DWORD WINAPI SearchEngine::CrawlThreadProc(LPVOID param) {
    SearchEngine* engine = reinterpret_cast<SearchEngine*>(param);
    engine->CrawlWorker();
    return 0;
}

// ---------------------------------------------------------------------
// Start Indexing
// ---------------------------------------------------------------------
void SearchEngine::StartIndexing() {
    if (indexing_active_.load()) return;

    // Reset state
    stop_requested_.store(false);
    indexing_active_.store(true);
    active_workers_.store(0);
    ResetEvent(completion_event_);

    // Clear previous cache
    AcquireSRWLockExclusive(&dir_lock_);
    directories_.clear();
    ReleaseSRWLockExclusive(&dir_lock_);

    AcquireSRWLockExclusive(&file_lock_);
    files_.clear();
    ReleaseSRWLockExclusive(&file_lock_);

    total_files_.store(0);
    dir_count_.store(0);

    // Enumerate drives and seed the work queue
    EnumerateDrives();

    // Launch crawler threads
    thread_count_ = Config::CRAWLER_THREADS;
    for (uint32_t i = 0; i < thread_count_; ++i) {
        threads_[i] = CreateThread(
            NULL, 65536, // Optimize stack size to 64KB
            CrawlThreadProc,
            this,
            0, NULL
        );
        if (threads_[i]) {
            // Set high priority for crawler threads
            SetThreadPriority(threads_[i], THREAD_PRIORITY_ABOVE_NORMAL);
        }
    }
}

// ---------------------------------------------------------------------
// Stop Indexing
// ---------------------------------------------------------------------
void SearchEngine::StopIndexing() {
    stop_requested_.store(true);

    // Wait for all threads to finish (with timeout)
    if (thread_count_ > 0) {
        WaitForMultipleObjects(thread_count_, threads_, TRUE, 5000);
        for (uint32_t i = 0; i < thread_count_; ++i) {
            if (threads_[i]) {
                CloseHandle(threads_[i]);
                threads_[i] = NULL;
            }
        }
        thread_count_ = 0;
    }

    indexing_active_.store(false);
}

// ---------------------------------------------------------------------
// Crawl Worker - Each thread runs this loop
// ---------------------------------------------------------------------
void SearchEngine::CrawlWorker() {
    std::wstring dir_path;
    std::vector<FileRecord> local_batch;
    local_batch.reserve(1000);

    while (!stop_requested_.load(std::memory_order_relaxed)) {
        // Increment active_workers_ before trying to pop to prevent premature termination race
        active_workers_.fetch_add(1, std::memory_order_acq_rel);

        if (work_queue_.try_pop(dir_path)) {
            ProcessDirectory(dir_path, local_batch);
            active_workers_.fetch_sub(1, std::memory_order_acq_rel);
        } else {
            active_workers_.fetch_sub(1, std::memory_order_acq_rel);
            
            // Queue is empty. Check if all workers are truly idle and queue is still empty
            if (active_workers_.load(std::memory_order_acquire) == 0 && work_queue_.empty()) {
                break; // Deterministic shutdown, no races!
            }
            Sleep(1); // Brief sleep to avoid busy-waiting
        }

        // Check cache limit
        if (total_files_.load(std::memory_order_relaxed) >= Config::MAX_CACHE_FILES) {
            break; // Cache full
        }
    }

    // Flush any remaining local batch items before terminating
    AddFileBatch(local_batch);

    // If this is the last active worker, signal completion
    if (active_workers_.load(std::memory_order_relaxed) == 0) {
        indexing_active_.store(false, std::memory_order_relaxed);
        SetEvent(completion_event_);
    }
}

// ---------------------------------------------------------------------
// Process a Single Directory (core hot path)
// ---------------------------------------------------------------------
void SearchEngine::ProcessDirectory(const std::wstring& dir_path, std::vector<FileRecord>& local_batch) {
    // Check cache limit before processing
    if (total_files_.load(std::memory_order_relaxed) >= Config::MAX_CACHE_FILES) {
        return;
    }

    // Build search pattern: dir_path + L"\\*"
    wchar_t search_buf[MAX_PATH + 4];
    size_t dir_len = dir_path.size();

    if (dir_len >= MAX_PATH) return; // Path too long, skip

    memcpy(search_buf, dir_path.c_str(), dir_len * sizeof(wchar_t));
    if (dir_len > 0 && search_buf[dir_len - 1] != L'\\') {
        search_buf[dir_len++] = L'\\';
    }
    search_buf[dir_len]     = L'*';
    search_buf[dir_len + 1] = L'\0';

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileExW(
        search_buf,
        FindExInfoBasic,           // Faster: don't retrieve short names
        &fd,
        FindExSearchNameMatch,
        NULL,
        FIND_FIRST_EX_LARGE_FETCH  // Batch fetch for performance
    );

    if (hFind == INVALID_HANDLE_VALUE) {
        return; // Access denied or empty
    }

    // Register this directory
    uint32_t current_dir_index = AddDirectory(dir_path);

    do {
        // Skip . and ..
        if (fd.cFileName[0] == L'.') {
            if (fd.cFileName[1] == L'\0') continue;
            if (fd.cFileName[1] == L'.' && fd.cFileName[2] == L'\0') continue;
        }

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            // Skip system/hidden directories that commonly cause access denied
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;

            // Build subdirectory path and push to work queue
            std::wstring sub_path;
            sub_path.reserve(dir_len + 1 + wcslen(fd.cFileName));
            sub_path = dir_path;
            if (sub_path.back() != L'\\') sub_path += L'\\';
            sub_path += fd.cFileName;

            work_queue_.push(std::move(sub_path));
        } else {
            // It's a file - add to local batch
            local_batch.push_back({current_dir_index, fd.cFileName});

            // Flush if batch is full
            if (local_batch.size() >= 1000) {
                AddFileBatch(local_batch);
            }

            // Check cache limit periodically
            if (total_files_.load(std::memory_order_relaxed) >= Config::MAX_CACHE_FILES) {
                break;
            }
        }
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);
}

// ---------------------------------------------------------------------
// Add a directory path to the directory list (thread-safe)
// ---------------------------------------------------------------------
uint32_t SearchEngine::AddDirectory(const std::wstring& path) {
    AcquireSRWLockExclusive(&dir_lock_);
    uint32_t idx = static_cast<uint32_t>(directories_.size());
    directories_.push_back(path);
    ReleaseSRWLockExclusive(&dir_lock_);
    dir_count_.fetch_add(1, std::memory_order_relaxed);
    return idx;
}

// ---------------------------------------------------------------------
// Add a file record to the cache (thread-safe)
// ---------------------------------------------------------------------
void SearchEngine::AddFile(uint32_t dir_index, const std::wstring& filename) {
    AcquireSRWLockExclusive(&file_lock_);
    files_.push_back({dir_index, filename});
    ReleaseSRWLockExclusive(&file_lock_);
    total_files_.fetch_add(1, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------
// Add a batch of file records to the cache (drastically reduces lock contention)
// ---------------------------------------------------------------------
void SearchEngine::AddFileBatch(std::vector<FileRecord>& local_batch) {
    if (local_batch.empty()) return;

    AcquireSRWLockExclusive(&file_lock_);
    
    if (files_.size() + local_batch.size() > Config::MAX_CACHE_FILES) {
        size_t allowed = Config::MAX_CACHE_FILES - files_.size();
        if (allowed > 0) {
            files_.insert(files_.end(), 
                          std::make_move_iterator(local_batch.begin()), 
                          std::make_move_iterator(local_batch.begin() + allowed));
        }
    } else {
        files_.insert(files_.end(), 
                      std::make_move_iterator(local_batch.begin()), 
                      std::make_move_iterator(local_batch.end()));
    }
    
    ReleaseSRWLockExclusive(&file_lock_);
    
    total_files_.fetch_add(local_batch.size(), std::memory_order_relaxed);
    local_batch.clear();
}

// ---------------------------------------------------------------------
// Build full path from dir_index + filename
// ---------------------------------------------------------------------
std::wstring SearchEngine::BuildFullPath(uint32_t dir_index, const std::wstring& filename) {
    std::wstring path;
    AcquireSRWLockShared(&dir_lock_);
    if (dir_index < directories_.size()) {
        path = directories_[dir_index];
    }
    ReleaseSRWLockShared(&dir_lock_);

    if (!path.empty() && path.back() != L'\\') {
        path += L'\\';
    }
    path += filename;
    return path;
}

// ---------------------------------------------------------------------
// Case-Insensitive Substring Match (hot loop - optimized)
// ---------------------------------------------------------------------
bool SearchEngine::ContainsCI(const wchar_t* haystack, size_t haystack_len,
                               const wchar_t* needle,   size_t needle_len) {
    if (needle_len == 0) return true;
    if (needle_len > haystack_len) return false;

    size_t limit = haystack_len - needle_len;
    for (size_t i = 0; i <= limit; ++i) {
        bool match = true;
        for (size_t j = 0; j < needle_len; ++j) {
            wchar_t h = haystack[i + j];
            wchar_t n = needle[j];
            
            // Fast ASCII lower-casing, fallback to standard towlower for Unicode
            if (h >= L'A' && h <= L'Z') h += 32;
            else if (h > 127) h = towlower(h);
            
            if (n >= L'A' && n <= L'Z') n += 32;
            else if (n > 127) n = towlower(n);

            if (h != n) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

// ---------------------------------------------------------------------
/// ---------------------------------------------------------------------
// Highly Optimized String Matching Helper
// ---------------------------------------------------------------------
static bool MatchString(const wchar_t* haystack, size_t haystack_len,
                        const wchar_t* needle, size_t needle_len,
                        SearchOptions::MatchType match_type, bool case_sensitive) {
    if (needle_len == 0) return true;
    if (needle_len > haystack_len) return false;

    if (case_sensitive) {
        switch (match_type) {
            case SearchOptions::MatchType::CONTAINS: {
                size_t limit = haystack_len - needle_len;
                for (size_t i = 0; i <= limit; ++i) {
                    if (wmemcmp(haystack + i, needle, needle_len) == 0) {
                        return true;
                    }
                }
                return false;
            }
            case SearchOptions::MatchType::STARTS_WITH: {
                return wmemcmp(haystack, needle, needle_len) == 0;
            }
            case SearchOptions::MatchType::ENDS_WITH: {
                return wmemcmp(haystack + (haystack_len - needle_len), needle, needle_len) == 0;
            }
            case SearchOptions::MatchType::EXACT: {
                return haystack_len == needle_len && wmemcmp(haystack, needle, needle_len) == 0;
            }
        }
    } else {
        // Case-insensitive matching. Note: needle is ALREADY lowercased in caller if case_sensitive is false.
        switch (match_type) {
            case SearchOptions::MatchType::CONTAINS: {
                return SearchEngine::ContainsCI(haystack, haystack_len, needle, needle_len);
            }
            case SearchOptions::MatchType::STARTS_WITH: {
                for (size_t i = 0; i < needle_len; ++i) {
                    wchar_t h = haystack[i];
                    if (h >= L'A' && h <= L'Z') h += 32;
                    else if (h > 127) h = towlower(h);
                    if (h != needle[i]) return false;
                }
                return true;
            }
            case SearchOptions::MatchType::ENDS_WITH: {
                size_t offset = haystack_len - needle_len;
                for (size_t i = 0; i < needle_len; ++i) {
                    wchar_t h = haystack[offset + i];
                    if (h >= L'A' && h <= L'Z') h += 32;
                    else if (h > 127) h = towlower(h);
                    if (h != needle[i]) return false;
                }
                return true;
            }
            case SearchOptions::MatchType::EXACT: {
                if (haystack_len != needle_len) return false;
                for (size_t i = 0; i < needle_len; ++i) {
                    wchar_t h = haystack[i];
                    if (h >= L'A' && h <= L'Z') h += 32;
                    else if (h > 127) h = towlower(h);
                    if (h != needle[i]) return false;
                }
                return true;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------
// Search - Parallel search across cached file records
// ---------------------------------------------------------------------
void SearchEngine::Search(const std::wstring& query, std::vector<SearchResult>& results, const SearchOptions& options) {
    results.clear();

    if (query.size() < Config::MIN_QUERY_LENGTH) return;

    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    // Convert query to lowercase once if case-insensitive
    std::wstring match_query = query;
    if (!options.case_sensitive) {
        for (auto& c : match_query) {
            if (c >= L'A' && c <= L'Z') c += 32;
            else if (c > 127) c = towlower(c);
        }
    }

    const wchar_t* needle     = match_query.c_str();
    const size_t   needle_len = match_query.size();

    // Take a snapshot of current file count to avoid locking during search
    AcquireSRWLockShared(&file_lock_);
    size_t file_count = files_.size();
    ReleaseSRWLockShared(&file_lock_);

    if (file_count == 0) {
        last_search_time_ms_ = 0.0;
        return;
    }

    // For small datasets, search single-threaded
    // For large datasets, split across multiple threads
    const uint32_t MAX_RESULTS = Config::MAX_DISPLAY_RESULTS;

    if (file_count < 10000) {
        // Single-threaded search for small caches
        AcquireSRWLockShared(&file_lock_);
        for (size_t i = 0; i < file_count && results.size() < MAX_RESULTS; ++i) {
            const FileRecord& rec = files_[i];
            
            std::wstring match_target;
            if (options.search_path) {
                match_target = BuildFullPath(rec.dir_index, rec.name);
            } else {
                match_target = rec.name;
            }

            if (MatchString(match_target.c_str(), match_target.size(), needle, needle_len, options.match_type, options.case_sensitive)) {
                SearchResult sr;
                sr.filename = rec.name;
                sr.full_path = options.search_path ? match_target : BuildFullPath(rec.dir_index, rec.name);
                results.push_back(std::move(sr));
            }
        }
        ReleaseSRWLockShared(&file_lock_);
    } else {
        // Multi-threaded parallel search
        const uint32_t num_threads = Config::SEARCH_THREADS;
        size_t chunk = file_count / num_threads;

        struct IntermediateResult {
            uint32_t     dir_index;
            std::wstring filename;
        };

        struct ThreadData {
            SearchEngine*                  engine;
            size_t                         start_idx;
            size_t                         end_idx;
            const wchar_t*                 needle;
            size_t                         needle_len;
            SearchOptions::MatchType       match_type;
            bool                           case_sensitive;
            bool                           search_path;
            std::vector<IntermediateResult> local_results;
            std::vector<SearchResult>       resolved_results;
        };

        std::vector<ThreadData> td(num_threads);
        std::vector<HANDLE>     handles(num_threads);

        for (uint32_t t = 0; t < num_threads; ++t) {
            td[t].engine         = this;
            td[t].start_idx      = t * chunk;
            td[t].end_idx        = (t == num_threads - 1) ? file_count : (t + 1) * chunk;
            td[t].needle         = needle;
            td[t].needle_len     = needle_len;
            td[t].match_type     = options.match_type;
            td[t].case_sensitive = options.case_sensitive;
            td[t].search_path    = options.search_path;
            td[t].local_results.reserve(MAX_RESULTS / num_threads + 1);

            handles[t] = CreateThread(NULL, 0,
                [](LPVOID param) -> DWORD {
                    ThreadData* data = reinterpret_cast<ThreadData*>(param);
                    SearchEngine* eng = data->engine;
                    const uint32_t local_max = Config::MAX_DISPLAY_RESULTS / Config::SEARCH_THREADS + 1;

                    AcquireSRWLockShared(&eng->file_lock_);
                    for (size_t i = data->start_idx;
                         i < data->end_idx && data->local_results.size() < local_max;
                         ++i)
                    {
                        const FileRecord& rec = eng->files_[i];
                        
                        if (data->search_path) {
                            std::wstring full_path = eng->BuildFullPath(rec.dir_index, rec.name);
                            if (MatchString(full_path.c_str(), full_path.size(),
                                            data->needle, data->needle_len,
                                            data->match_type, data->case_sensitive))
                            {
                                data->local_results.push_back({rec.dir_index, rec.name});
                            }
                        } else {
                            if (MatchString(rec.name.c_str(), rec.name.size(),
                                            data->needle, data->needle_len,
                                            data->match_type, data->case_sensitive))
                            {
                                data->local_results.push_back({rec.dir_index, rec.name});
                            }
                        }
                    }
                    ReleaseSRWLockShared(&eng->file_lock_);

                    // Now resolve full paths (needs dir_lock_ but not file_lock_, zero std::stoul parsing overhead!)
                    data->resolved_results.reserve(data->local_results.size());
                    for (const auto& lr : data->local_results) {
                        SearchResult sr;
                        sr.filename  = lr.filename;
                        sr.full_path = eng->BuildFullPath(lr.dir_index, lr.filename);
                        data->resolved_results.push_back(std::move(sr));
                    }

                    return 0;
                },
                &td[t], 0, NULL
            );
        }

        // Wait for all search threads
        WaitForMultipleObjects(num_threads, handles.data(), TRUE, INFINITE);

        // Merge results
        for (uint32_t t = 0; t < num_threads; ++t) {
            CloseHandle(handles[t]);
            for (auto& sr : td[t].resolved_results) {
                if (results.size() >= MAX_RESULTS) break;
                results.push_back(std::move(sr));
            }
            if (results.size() >= MAX_RESULTS) break;
        }
    }

    QueryPerformanceCounter(&end);
    last_search_time_ms_ = static_cast<double>(end.QuadPart - start.QuadPart)
                           / static_cast<double>(freq.QuadPart) * 1000.0;
}

// ---------------------------------------------------------------------
// Open file location in Windows Explorer
// ---------------------------------------------------------------------
bool SearchEngine::OpenInExplorer(const std::wstring& path) {
    // Check if the path exists
    DWORD attr = GetFileAttributesW(path.c_str());

    if (attr == INVALID_FILE_ATTRIBUTES) {
        return false;
    }

    // Build the command: explorer /select,"<path>"
    std::wstring cmd = L"/select,\"" + path + L"\"";
    ShellExecuteW(NULL, L"open", L"explorer.exe", cmd.c_str(), NULL, SW_SHOWNORMAL);
    return true;
}
