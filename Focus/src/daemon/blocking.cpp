#include "blocking.h"
#include "../common/utils.h"
#include <windows.h>
#include <tlhelp32.h>
#include <thread>
#include <atomic>
#include <algorithm>
#include <mutex>

static std::atomic<bool> s_monitorActive{false};
static bool s_blockAllExceptAllowed = false;
static std::vector<std::wstring> s_allowedApps;
static std::vector<std::wstring> s_blockedApps;
static std::thread s_monitorThread;
static std::mutex s_monitorMutex;

static void killProcessById(DWORD pid, const std::wstring& name) {
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess != NULL) {
        if (TerminateProcess(hProcess, 0)) {
            logMessage(L"Daemon: Terminated blocked process: " + name + L" (PID: " + std::to_wstring(pid) + L")");
        } else {
            logMessage(L"Daemon: Failed to terminate process: " + name + L" (PID: " + std::to_wstring(pid) + L"), Error: " + std::to_wstring(GetLastError()));
        }
        CloseHandle(hProcess);
    }
}

// Checks if a process is a critical system process or user-allowed work process
static bool isSystemOrAllowedProcess(const std::wstring& name) {
    static const std::vector<std::wstring> systemApps = {
        // Windows Critical Core Services & UI
        L"explorer.exe", L"taskmgr.exe", L"conhost.exe", L"cmd.exe", L"powershell.exe", L"pwsh.exe",
        L"ctfmon.exe", L"winlogon.exe", L"services.exe", L"lsass.exe", L"svchost.exe",
        L"wininit.exe", L"csrss.exe", L"smss.exe", L"spoolsv.exe", L"sihost.exe",
        L"fontdrvhost.exe", L"dwm.exe", L"ShellExperienceHost.exe", L"SearchHost.exe",
        L"StartMenuExperienceHost.exe", L"SystemSettings.exe", L"focus.exe", L"antigravity.exe",
        
        // Windows System Helpers (To prevent OS/UI crashes)
        L"RuntimeBroker.exe", L"dllhost.exe", L"backgroundTaskHost.exe", L"taskhostw.exe",
        L"unsecapp.exe", L"ShellHost.exe", L"TextInputHost.exe", L"ApplicationFrameHost.exe",
        L"SearchProtocolHost.exe", L"UserOOBEBroker.exe", L"smartscreen.exe",
        L"OpenConsole.exe", L"WindowsTerminal.exe", L"SecurityHealthService.exe", 
        L"SecurityHealthSystray.exe", L"consent.exe", L"CredentialUIBroker.exe",
        
        // Development Tools, Compilers, Debuggers & Helper Processes
        L"g++.exe", L"cc1plus.exe", L"make.exe", L"node.exe", L"npm.exe",
        L"cpptools.exe", L"cpptools-srv.exe", L"cpptools-wordexp.exe", L"rg.exe",
        L"git.exe", L"git-remote-https.exe", L"wsl.exe", L"wslhost.exe",
        L"clangd.exe", L"ninja.exe", L"cmake.exe", L"python.exe", L"pythonw.exe",
        L"pip.exe", L"language_server.exe", L"rust-analyzer.exe", L"cargo.exe"
    };
    
    for (const auto& app : systemApps) {
        if (_wcsicmp(name.c_str(), app.c_str()) == 0) return true;
    }
    
    std::lock_guard<std::mutex> lock(s_monitorMutex);
    for (const auto& app : s_allowedApps) {
        if (_wcsicmp(name.c_str(), app.c_str()) == 0) return true;
    }
    return false;
}

static void monitorLoop() {
    logMessage(L"Daemon: Process monitor thread started. Mode: " + std::wstring(s_blockAllExceptAllowed ? L"WHITELIST" : L"BLACKLIST"));
    while (s_monitorActive) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry;
            entry.dwSize = sizeof(PROCESSENTRY32W);
            if (Process32FirstW(snapshot, &entry)) {
                do {
                    std::wstring exeName = entry.szExeFile;
                    
                    if (s_blockAllExceptAllowed) {
                        // Whitelist Mode: Terminate if NOT system or allowed work process
                        if (!isSystemOrAllowedProcess(exeName)) {
                            // Don't kill kernel pseudoprocesses or names lacking .exe extensions
                            if (exeName != L"System" && exeName != L"Idle" && exeName.find(L".exe") != std::wstring::npos) {
                                killProcessById(entry.th32ProcessID, exeName);
                            }
                        }
                    } else {
                        // Blacklist Mode: Terminate if blacklisted
                        std::lock_guard<std::mutex> lock(s_monitorMutex);
                        auto it = std::find_if(s_blockedApps.begin(), s_blockedApps.end(), 
                            [&exeName](const std::wstring& blockedName) {
                                return _wcsicmp(exeName.c_str(), blockedName.c_str()) == 0;
                            });
                        
                        if (it != s_blockedApps.end()) {
                            killProcessById(entry.th32ProcessID, exeName);
                        }
                    }
                } while (Process32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);
        }
        
        Sleep(500);
    }
    logMessage(L"Daemon: Process monitor thread stopped.");
}

void startProcessMonitor(bool blockAllExceptAllowed, const std::vector<std::wstring>& allowedApps, const std::vector<std::wstring>& blockedApps) {
    if (s_monitorActive) return;
    
    {
        std::lock_guard<std::mutex> lock(s_monitorMutex);
        s_blockAllExceptAllowed = blockAllExceptAllowed;
        s_allowedApps = allowedApps;
        s_blockedApps = blockedApps;
        
        // Ensure antigravity.exe is always whitelisted
        if (s_blockAllExceptAllowed) {
            if (std::find(s_allowedApps.begin(), s_allowedApps.end(), L"antigravity.exe") == s_allowedApps.end()) {
                s_allowedApps.push_back(L"antigravity.exe");
            }
        }
    }
    
    s_monitorActive = true;
    s_monitorThread = std::thread(monitorLoop);
}

void stopProcessMonitor() {
    if (!s_monitorActive) return;
    
    s_monitorActive = false;
    if (s_monitorThread.joinable()) {
        s_monitorThread.join();
    }
    
    {
        std::lock_guard<std::mutex> lock(s_monitorMutex);
        s_allowedApps.clear();
        s_blockedApps.clear();
    }
}

void updateProcessMonitorLists(bool blockAllExceptAllowed, const std::vector<std::wstring>& allowedApps, const std::vector<std::wstring>& blockedApps) {
    std::lock_guard<std::mutex> lock(s_monitorMutex);
    s_blockAllExceptAllowed = blockAllExceptAllowed;
    s_allowedApps = allowedApps;
    s_blockedApps = blockedApps;
    
    // Ensure antigravity.exe is always whitelisted
    if (s_blockAllExceptAllowed) {
        if (std::find(s_allowedApps.begin(), s_allowedApps.end(), L"antigravity.exe") == s_allowedApps.end()) {
            s_allowedApps.push_back(L"antigravity.exe");
        }
    }
    logMessage(L"Daemon: Whitelist/Blacklist process lists dynamically updated.");
}

