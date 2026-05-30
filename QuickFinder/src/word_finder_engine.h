#pragma once
/***********************************************************************
 * QuickFinder - Word Finder Engine Header
 * 
 * Architecture:
 *   - Filters text files from the SearchEngine's index
 *   - Memory-maps files for ultra-fast reading
 *   - ThreadPool for parallel content searching
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
#include <unordered_set>
#include "search_engine.h"

// =====================================================================
// WordFinderEngine
// =====================================================================
class WordFinderEngine {
public:
    WordFinderEngine(SearchEngine* engine);
    ~WordFinderEngine();

    // Search file contents for a query
    void Search(const std::wstring& query, std::vector<SearchResult>& results, const SearchOptions& options = {});

    double GetLastSearchTimeMs() const { return last_search_time_ms_; }

private:
    SearchEngine* engine_;
    double last_search_time_ms_ = 0.0;
    std::unordered_set<std::wstring> text_extensions_;

    void InitTextExtensions();
    bool IsTextFile(const std::wstring& filename) const;
    bool SearchFileContent(const std::wstring& full_path, const std::string& needle_utf8, const SearchOptions& options) const;
};
