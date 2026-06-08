// ============================================================================
// config.cpp — TOML configuration file parser and writer
//
// Hand-rolled minimal TOML parser that handles exactly the keys used by
// LiveWallpaper. No external dependencies. Supports:
//   - Section headers: [wallpaper], [behavior], [advanced]
//   - String values:   key = "value" (with \\ escape handling)
//   - Integer values:  key = 24
//   - Boolean values:  key = true / false
//   - Comments:        # line comments
//   - Blank lines:     ignored
//
// The parser maps by key name (all keys are unique across sections), so
// section headers are parsed but not required for correct operation.
// ============================================================================

#include "config.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace lw {

// ============================================================================
// GetDefaultConfigPath
//
// Returns %APPDATA%\LiveWallpaper\config.toml.
// Uses SHGetFolderPathW (available on all supported Windows versions).
// ============================================================================
std::wstring GetDefaultConfigPath()
{
    wchar_t appData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
        LOG_ERROR("SHGetFolderPathW failed for APPDATA");
        return L"config.toml";  // Fallback to current directory
    }

    std::wstring path(appData);
    path += L"\\LiveWallpaper\\config.toml";
    return path;
}

// ============================================================================
// Internal helpers
// ============================================================================

/// Trim leading and trailing whitespace from a C string in-place.
/// Returns pointer to the first non-whitespace character; modifies the
/// string to null-terminate before trailing whitespace.
static char* TrimInPlace(char* s)
{
    // Skip leading whitespace
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') ++s;

    // Find end and trim trailing whitespace
    char* end = s + strlen(s) - 1;
    while (end > s && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        --end;
    }
    return s;
}

/// Strip surrounding double quotes from a value string.
/// Also handles standard TOML backslash escape sequences.
/// Returns the unquoted, unescaped content.
static std::string StripQuotes(const char* val)
{
    std::string result;
    size_t len = strlen(val);

    // Check for surrounding quotes
    if (len >= 2 && val[0] == '"' && val[len - 1] == '"') {
        // Parse the interior, handling escape sequences
        for (size_t i = 1; i < len - 1; ++i) {
            if (val[i] == '\\' && i + 1 < len - 1) {
                char next = val[i + 1];
                if (next == '\\') {
                    result += '\\';
                    ++i;  // Skip the escaped character
                } else if (next == '"') {
                    result += '"';
                    ++i;
                } else if (next == 'n') {
                    result += '\n';
                    ++i;
                } else if (next == 't') {
                    result += '\t';
                    ++i;
                } else {
                    // Unknown escape — keep as-is
                    result += val[i];
                }
            } else {
                result += val[i];
            }
        }
    } else {
        // No quotes — use value as-is
        result = val;
    }
    return result;
}

/// Convert a narrow (UTF-8 / ANSI) string to a wide string.
static std::wstring NarrowToWide(const std::string& narrow)
{
    if (narrow.empty()) return {};

    int needed = MultiByteToWideChar(CP_UTF8, 0,
                                     narrow.c_str(), static_cast<int>(narrow.size()),
                                     nullptr, 0);
    if (needed <= 0) return {};

    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
                        narrow.c_str(), static_cast<int>(narrow.size()),
                        &wide[0], needed);
    return wide;
}

/// Convert a wide string to a narrow (UTF-8) string.
static std::string WideToNarrow(const std::wstring& wide)
{
    if (wide.empty()) return {};

    int needed = WideCharToMultiByte(CP_UTF8, 0,
                                     wide.c_str(), static_cast<int>(wide.size()),
                                     nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};

    std::string narrow(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0,
                        wide.c_str(), static_cast<int>(wide.size()),
                        &narrow[0], needed, nullptr, nullptr);
    return narrow;
}

/// Clamp an integer to [lo, hi].
static int ClampInt(int val, int lo, int hi)
{
    if (val < lo) return lo;
    if (val > hi) return hi;
    return val;
}

// ============================================================================
// LoadConfig
//
// Reads a minimal TOML file line by line and populates an AppConfig struct.
// If the file doesn't exist or can't be opened, returns defaults — this is
// the expected behavior on first launch.
//
// The parser is intentionally lenient:
//   - Unknown keys are silently ignored
//   - Malformed lines are skipped with a warning
//   - Values outside valid ranges are clamped
// ============================================================================
AppConfig LoadConfig(const wchar_t* path)
{
    AppConfig config;  // Default-initialized with sensible defaults

    std::wstring filePath = path ? std::wstring(path) : GetDefaultConfigPath();

    FILE* f = _wfopen(filePath.c_str(), L"r");
    if (!f) {
        LOG_INFO("Config file not found at specified path, using defaults");
        return config;
    }

    LOG_INFO("Loading config from file");

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char* trimmed = TrimInPlace(line);

        // Skip empty lines
        if (trimmed[0] == '\0') continue;

        // Skip comments
        if (trimmed[0] == '#') continue;

        // Skip section headers — we don't need them since all keys are unique
        if (trimmed[0] == '[') continue;

        // Find the '=' separator
        char* eq = strchr(trimmed, '=');
        if (!eq) {
            LOG_WARN("Malformed config line (no '='): %s", trimmed);
            continue;
        }

        // Split into key and value
        *eq = '\0';
        char* key = TrimInPlace(trimmed);
        char* val = TrimInPlace(eq + 1);

        // Map known keys to config fields
        if (strcmp(key, "path") == 0) {
            std::string narrow = StripQuotes(val);
            config.wallpaperPath = NarrowToWide(narrow);
        }
        else if (strcmp(key, "fps") == 0) {
            config.fps = ClampInt(atoi(val), 1, 60);
        }
        else if (strcmp(key, "idle_timeout_minutes") == 0) {
            config.idleTimeoutMinutes = ClampInt(atoi(val), 0, 60);
        }
        else if (strcmp(key, "pause_on_battery") == 0) {
            config.pauseOnBattery = (strcmp(val, "true") == 0);
        }
        else if (strcmp(key, "pause_on_fullscreen") == 0) {
            config.pauseOnFullscreen = (strcmp(val, "true") == 0);
        }
        else if (strcmp(key, "gpu_preference") == 0) {
            std::string pref = StripQuotes(val);
            if (pref == "integrated" || pref == "discrete" || pref == "auto") {
                config.gpuPreference = pref;
            } else {
                LOG_WARN("Unknown gpu_preference '%s', defaulting to 'integrated'", pref.c_str());
            }
        }
        else {
            LOG_DEBUG("Unknown config key: %s", key);
        }
    }

    fclose(f);

    LOG_INFO("Config loaded: fps=%d, idle=%dmin, battery=%s, fullscreen=%s, gpu=%s",
             config.fps, config.idleTimeoutMinutes,
             config.pauseOnBattery ? "pause" : "ignore",
             config.pauseOnFullscreen ? "pause" : "ignore",
             config.gpuPreference.c_str());

    return config;
}

// ============================================================================
// SaveConfig
//
// Writes the AppConfig to a TOML file matching the project's default_config
// format. Creates the parent directory if it doesn't exist.
//
// Backslashes in the wallpaper path are doubled for TOML string escaping:
//   D:\Wallpapers\video.mp4  →  "D:\\Wallpapers\\video.mp4"
// ============================================================================
bool SaveConfig(const AppConfig& config, const wchar_t* path)
{
    std::wstring filePath = path ? std::wstring(path) : GetDefaultConfigPath();

    // Ensure parent directory exists.
    // Find last backslash and create directory up to that point.
    size_t lastSlash = filePath.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        std::wstring dirPath = filePath.substr(0, lastSlash);
        CreateDirectoryW(dirPath.c_str(), nullptr);
        // CreateDirectoryW returns FALSE if dir already exists — that's fine
    }

    FILE* f = _wfopen(filePath.c_str(), L"w");
    if (!f) {
        LOG_ERROR("Failed to open config file for writing");
        return false;
    }

    // Convert wallpaper path to narrow UTF-8 with escaped backslashes
    std::string narrowPath = WideToNarrow(config.wallpaperPath);
    std::string escapedPath;
    escapedPath.reserve(narrowPath.size() + 16);
    for (char c : narrowPath) {
        if (c == '\\') {
            escapedPath += "\\\\";
        } else {
            escapedPath += c;
        }
    }

    // Write TOML format
    fprintf(f, "[wallpaper]\n");
    fprintf(f, "path = \"%s\"\n", escapedPath.c_str());
    fprintf(f, "fps = %d\n", config.fps);
    fprintf(f, "\n");
    fprintf(f, "[behavior]\n");
    fprintf(f, "idle_timeout_minutes = %d\n", config.idleTimeoutMinutes);
    fprintf(f, "pause_on_battery = %s\n", config.pauseOnBattery ? "true" : "false");
    fprintf(f, "pause_on_fullscreen = %s\n", config.pauseOnFullscreen ? "true" : "false");
    fprintf(f, "\n");
    fprintf(f, "[advanced]\n");
    fprintf(f, "gpu_preference = \"%s\"\n", config.gpuPreference.c_str());

    fclose(f);

    LOG_INFO("Config saved successfully");
    return true;
}

} // namespace lw
