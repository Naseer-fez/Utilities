#pragma once
#include <string>
#include <vector>

struct SmtpConfig {
    std::wstring smtpUser;
    std::wstring smtpPassword;
    std::wstring emailTo1;
    std::wstring emailTo2;
};

struct Profile {
    std::wstring name;
    bool blockAllExceptAllowed;
    std::vector<std::wstring> allowedApps;
    std::vector<std::wstring> appsToClose;
    std::vector<std::wstring> appsToLaunch;
    std::wstring wallpaperPath;
    int volume;
    int durationMinutes;
};

std::vector<Profile> loadProfiles(const std::wstring& configPath);
bool loadSmtpConfig(const std::wstring& configPath, SmtpConfig& smtp);
