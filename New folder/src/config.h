#pragma once

// ============================================================================
// config.h — Application configuration for LiveWallpaper
//
// Defines the AppConfig struct and TOML file I/O functions.
// Config is stored at %APPDATA%\LiveWallpaper\config.toml in a minimal
// TOML format. The parser is hand-rolled (~200 lines) and only handles
// the specific keys we use — no full TOML spec compliance needed.
//
// Config is loaded once at startup and can be saved from the settings
// dialog. The struct is passed by value/reference — no heap allocation.
// ============================================================================

#include "utils.h"
#include <string>

namespace lw {

/// Application configuration — maps 1:1 to config.toml keys.
/// All fields have sensible defaults so a missing config file is fine.
struct AppConfig {
    // [wallpaper]
    std::wstring wallpaperPath;     // Absolute path to .mp4 file (empty = none)
    int fps = 24;                   // Playback frame rate, clamped to [1, 60]

    // [behavior]
    int idleTimeoutMinutes = 5;     // Minutes of idle before freezing, 0 = disabled
    bool pauseOnBattery = true;     // Freeze wallpaper when on battery power
    bool pauseOnFullscreen = true;  // Freeze wallpaper when fullscreen app detected

    // [advanced]
    std::string gpuPreference = "integrated";  // "integrated", "discrete", or "auto"
};

/// Load configuration from a TOML file.
/// If path is nullptr, uses GetDefaultConfigPath().
/// Returns a default-initialized AppConfig if the file doesn't exist or
/// can't be parsed — never fails, always returns a usable config.
AppConfig LoadConfig(const wchar_t* path = nullptr);

/// Save configuration to a TOML file.
/// If path is nullptr, uses GetDefaultConfigPath().
/// Creates parent directories if they don't exist.
/// Returns true on success, false on write failure.
bool SaveConfig(const AppConfig& config, const wchar_t* path = nullptr);

/// Returns the default config file path: %APPDATA%\LiveWallpaper\config.toml
std::wstring GetDefaultConfigPath();

} // namespace lw
