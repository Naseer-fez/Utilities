/***********************************************************************
 * QuickFinder - Word Finder Engine Implementation
 ***********************************************************************/

#include "word_finder_engine.h"
#include <windows.h>
#include <shlwapi.h>
#include <algorithm>
#include <thread>
#include <mutex>

#pragma comment(lib, "shlwapi.lib")

// =====================================================================
// Helper: Convert wide string to UTF-8
// =====================================================================
static std::string WStringToUTF8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// =====================================================================
// WordFinderEngine Implementation
// =====================================================================

WordFinderEngine::WordFinderEngine(SearchEngine* engine) : engine_(engine) {
    InitTextExtensions();
}

WordFinderEngine::~WordFinderEngine() {
}

void WordFinderEngine::InitTextExtensions() {
    const wchar_t* exts[] = {
        L".txt", L".md", L".csv", L".json", L".xml", L".html", L".htm", L".css", L".js", L".ts", L".jsx", L".tsx",
        L".cpp", L".c", L".h", L".hpp", L".cs", L".java", L".py", L".rb", L".php", L".go", L".rs", L".swift",
        L".bat", L".cmd", L".ps1", L".sh", L".ini", L".cfg", L".conf", L".yml", L".yaml", L".toml", L".sql",
        L".log", L".env", L".gitignore"
    };
    for (const auto& ext : exts) {
        text_extensions_.insert(ext);
    }
}

bool WordFinderEngine::IsTextFile(const std::wstring& filename) const {
    size_t dot_pos = filename.find_last_of(L'.');
    if (dot_pos == std::wstring::npos) return false; // No extension, assume binary for safety

    std::wstring ext = filename.substr(dot_pos);
    for (auto& c : ext) {
        if (c >= L'A' && c <= L'Z') c += 32;
        else if (c > 127) c = towlower(c);
    }
    
    return text_extensions_.find(ext) != text_extensions_.end();
}

// Memory-mapped file search
bool WordFinderEngine::SearchFileContent(const std::wstring& full_path, const std::string& needle, const SearchOptions& options) const {
    if (needle.empty()) return false;

    HANDLE hFile = CreateFileW(full_path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart == 0) {
        CloseHandle(hFile);
        return false;
    }

    HANDLE hMap = CreateFileMappingW(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!hMap) {
        CloseHandle(hFile);
        return false;
    }

    const char* pData = static_cast<const char*>(MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0));
    if (!pData) {
        CloseHandle(hMap);
        CloseHandle(hFile);
        return false;
    }

    bool found = false;
    size_t len = static_cast<size_t>(fileSize.QuadPart);
    
    // Very basic and fast strstr search for UTF-8/ASCII data.
    // For production, a custom Boyer-Moore or similar is better, but this is fast enough for memory-mapped files.
    if (options.case_sensitive) {
        // Fast byte-level search using string_view
        std::string_view sv(pData, len);
        if (sv.find(needle) != std::string_view::npos) found = true;
    } else {
        static char lower_map[256];
        static bool init = false;
        if (!init) {
            for (int i = 0; i < 256; ++i) {
                lower_map[i] = (i >= 'A' && i <= 'Z') ? (char)(i + 32) : (char)i;
            }
            init = true;
        }

        std::string lower_needle = needle;
        for (char& c : lower_needle) {
            c = lower_map[(unsigned char)c];
        }
        size_t n_len = lower_needle.size();
        if (n_len <= len) {
            size_t limit = len - n_len;
            for (size_t i = 0; i <= limit; ++i) {
                if (lower_map[(unsigned char)pData[i]] == lower_needle[0]) {
                    bool match = true;
                    for (size_t j = 1; j < n_len; ++j) {
                        if (lower_map[(unsigned char)pData[i + j]] != lower_needle[j]) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        found = true;
                        break;
                    }
                }
            }
        }
    }

    UnmapViewOfFile(pData);
    CloseHandle(hMap);
    CloseHandle(hFile);

    return found;
}

void WordFinderEngine::Search(const std::wstring& query, std::vector<SearchResult>& results, const SearchOptions& options) {
    results.clear();
    if (query.empty() || !engine_) return;

    LARGE_INTEGER freq, start, end;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&start);

    std::string needle_utf8 = WStringToUTF8(query);

    // Get all files from SearchEngine
    std::vector<FileRecord> text_files;
    
    size_t total_files = 0;
    AcquireSRWLockShared(&engine_->file_lock_);
    total_files = engine_->files_.size();
    ReleaseSRWLockShared(&engine_->file_lock_);
    
    text_files.reserve(total_files / 10); // Estimate 10% are text
    
    for (size_t i = 0; i < total_files; ) {
        AcquireSRWLockShared(&engine_->file_lock_);
        size_t chunk_end = i + 100; // REDUCED from 10000 to prevent starvation
        if (chunk_end > engine_->files_.size()) chunk_end = engine_->files_.size();
        if (i >= chunk_end) {
            ReleaseSRWLockShared(&engine_->file_lock_);
            break;
        }
        for (; i < chunk_end; ++i) {
            if (IsTextFile(engine_->files_[i].name)) {
                text_files.push_back(engine_->files_[i]);
            }
        }
        ReleaseSRWLockShared(&engine_->file_lock_);
    }

    if (text_files.empty()) {
        last_search_time_ms_ = 0.0;
        return;
    }

    const uint32_t MAX_RESULTS = Config::MAX_DISPLAY_RESULTS;
    const uint32_t num_threads = Config::SEARCH_THREADS;
    size_t chunk = text_files.size() / num_threads;
    if (chunk == 0) chunk = 1;

    struct ThreadData {
        WordFinderEngine*              engine;
        const std::vector<FileRecord>* files;
        size_t                         start_idx;
        size_t                         end_idx;
        std::string                    needle;
        SearchOptions                  options;
        std::vector<SearchResult>      local_results;
    };

    std::vector<ThreadData> td(num_threads);
    std::vector<HANDLE> handles;

    for (uint32_t t = 0; t < num_threads; ++t) {
        size_t start_idx = t * chunk;
        if (start_idx >= text_files.size()) break;
        
        size_t end_idx = (t == num_threads - 1) ? text_files.size() : (t + 1) * chunk;
        if (end_idx > text_files.size()) end_idx = text_files.size();

        td[t].engine = this;
        td[t].files = &text_files;
        td[t].start_idx = start_idx;
        td[t].end_idx = end_idx;
        td[t].needle = needle_utf8;
        td[t].options = options;
        
        HANDLE hThread = CreateThread(NULL, 0, [](LPVOID param) -> DWORD {
            ThreadData* data = reinterpret_cast<ThreadData*>(param);
            const uint32_t local_max = Config::MAX_DISPLAY_RESULTS / Config::SEARCH_THREADS + 1;

            for (size_t i = data->start_idx; i < data->end_idx && data->local_results.size() < local_max; ++i) {
                const FileRecord& rec = (*data->files)[i];
                std::wstring full_path = data->engine->engine_->BuildFullPath(rec.dir_index, rec.name);
                
                if (data->engine->SearchFileContent(full_path, data->needle, data->options)) {
                    SearchResult sr;
                    sr.filename = rec.name;
                    sr.full_path = full_path;
                    data->local_results.push_back(std::move(sr));
                }
            }
            return 0;
        }, &td[t], 0, NULL);
        
        if (hThread) {
            SetThreadPriority(hThread, THREAD_PRIORITY_BELOW_NORMAL);
            handles.push_back(hThread);
        }
    }

    if (!handles.empty()) {
        WaitForMultipleObjects((DWORD)handles.size(), handles.data(), TRUE, INFINITE);
        for (HANDLE h : handles) CloseHandle(h);

        for (const auto& data : td) {
            for (const auto& sr : data.local_results) {
                if (results.size() >= MAX_RESULTS) break;
                results.push_back(sr);
            }
            if (results.size() >= MAX_RESULTS) break;
        }
    }

    QueryPerformanceCounter(&end);
    last_search_time_ms_ = static_cast<double>(end.QuadPart - start.QuadPart)
                           / static_cast<double>(freq.QuadPart) * 1000.0;
}
