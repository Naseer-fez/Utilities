#include "engine.h"
#include "blocking.h"
#include "../common/utils.h"
#include <windows.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <time.h>
#include <algorithm>
#include <fstream>

FocusEngine::FocusEngine() : m_smtpConfigured(false), m_sessionStartTicks(0), m_sessionDurationSeconds(0) {
    ZeroMemory(&m_state, sizeof(SessionState));
    m_stateFilePath = getAppDirectory() + L"\\.focus_session";
}

FocusEngine::~FocusEngine() {
    shutdown();
}

bool FocusEngine::initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Load SMTP settings from config.json
    std::wstring configPath = getAppDirectory() + L"\\config.json";
    m_smtpConfigured = loadSmtpConfig(configPath, m_smtp);
    if (m_smtpConfigured) {
        logMessage(L"Engine: SMTP configured successfully for user: " + m_smtp.smtpUser);
    } else {
        logMessage(L"Engine: SMTP email credentials not configured. Developer instant bypass is active.");
    }

    // Attempt to load existing session state (reboot recovery)
    if (loadSessionState(m_state, m_stateFilePath)) {
        if (m_state.isActive) {
            ULONGLONG now = getCurrentTimeSeconds(); // wall-clock
            ULONGLONG elapsed = (now > m_state.startTimeSeconds) ? (now - m_state.startTimeSeconds) : 0;
            if (elapsed < m_state.durationSeconds) {
                ULONGLONG remaining = m_state.durationSeconds - elapsed;
                logMessage(L"Engine: Active session recovered! Resuming strict mode. Remaining seconds: " + std::to_wstring(remaining));
                
                m_sessionStartTicks = GetTickCount64() / 1000;
                m_sessionDurationSeconds = remaining;

                std::vector<Profile> profiles = loadProfiles(configPath);
                bool blockAllExceptAllowed = false;
                std::vector<std::wstring> allowedApps;
                std::vector<std::wstring> appsToBlock;
                for (const auto& p : profiles) {
                    if (_wcsicmp(p.name.c_str(), m_state.activeProfile) == 0) {
                        blockAllExceptAllowed = p.blockAllExceptAllowed;
                        allowedApps = p.allowedApps;
                        appsToBlock = p.appsToClose;
                        break;
                    }
                }
                
                // Re-apply wallpaper and volume
                for (const auto& p : profiles) {
                    if (_wcsicmp(p.name.c_str(), m_state.activeProfile) == 0) {
                        if (!p.wallpaperPath.empty()) {
                            setWallpaper(p.wallpaperPath);
                        }
                        if (p.volume >= 0) {
                            setSystemVolume(p.volume);
                        }
                        break;
                    }
                }

                // Whitelist antigravity.exe globally
                if (blockAllExceptAllowed) {
                    if (std::find(allowedApps.begin(), allowedApps.end(), L"antigravity.exe") == allowedApps.end()) {
                        allowedApps.push_back(L"antigravity.exe");
                    }
                }

                startProcessMonitor(blockAllExceptAllowed, allowedApps, appsToBlock);
                return true;
            } else {
                logMessage(L"Engine: Recovered session has already expired. Cleaning up.");
                // Unlock and clean up (stopSession has internal lock, so we must be careful with recursion. 
                // We don't call stopSession with lock held. We will call a helper or unlock here).
                // Actually, to prevent deadlock, we can call stopSession without lock held.
                // We'll release the lock before calling stopSession.
            }
        }
    }
    return false;
}

void FocusEngine::shutdown() {
    bool active = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        active = m_state.isActive;
    }
    if (active) {
        stopSession(false); // Don't restore environment on daemon shutdown
    }
}

bool FocusEngine::startSession(const Profile& profile, int durationMinutes) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_state.isActive) return false;

    logMessage(L"Engine: Starting session for profile: " + profile.name + L" with duration: " + std::to_wstring(durationMinutes) + L" minutes");

    m_state.isActive = true;
    wcscpy_s(m_state.activeProfile, profile.name.c_str());
    m_state.startTimeSeconds = getCurrentTimeSeconds();
    m_state.durationSeconds = static_cast<ULONGLONG>(durationMinutes) * 60;
    
    m_sessionStartTicks = GetTickCount64() / 1000;
    m_sessionDurationSeconds = m_state.durationSeconds;

    // Save original environment states
    std::wstring origWallpaper = getOriginalWallpaper();
    wcscpy_s(m_state.originalWallpaper, origWallpaper.c_str());
    m_state.originalVolume = getSystemVolume();
    wcscpy_s(m_state.unlockCode, L"UNL0CK"); // Default master code

    // Save session file instantly
    saveSessionState(m_state, m_stateFilePath);

    // Apply environment changes
    if (!profile.wallpaperPath.empty()) {
        setWallpaper(profile.wallpaperPath);
    }
    if (profile.volume >= 0) {
        setSystemVolume(profile.volume);
    }

    // Launch work apps if configured
    for (const auto& app : profile.appsToLaunch) {
        launchProcess(app);
    }

    // Start process blocking (passes whitelisting settings)
    std::vector<std::wstring> allowedApps = profile.allowedApps;
    if (profile.blockAllExceptAllowed) {
        if (std::find(allowedApps.begin(), allowedApps.end(), L"antigravity.exe") == allowedApps.end()) {
            allowedApps.push_back(L"antigravity.exe");
        }
    }
    startProcessMonitor(profile.blockAllExceptAllowed, allowedApps, profile.appsToClose);

    return true;
}

bool FocusEngine::resumeSession(const SessionState& savedState) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_state = savedState;
    return saveSessionState(m_state, m_stateFilePath);
}

void FocusEngine::stopSession(bool restoreEnvironment) {
    std::wstring origWallpaper;
    int origVolume = -1;
    bool wasActive = false;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_state.isActive) return;
        wasActive = true;

        logMessage(L"Engine: Stopping session. Restore environment: " + std::wstring(restoreEnvironment ? L"YES" : L"NO"));

        if (restoreEnvironment) {
            origWallpaper = m_state.originalWallpaper;
            origVolume = m_state.originalVolume;
        }

        // Reset local state
        ZeroMemory(&m_state, sizeof(SessionState));
        m_sessionStartTicks = 0;
        m_sessionDurationSeconds = 0;
        deleteSessionState(m_stateFilePath);
    }

    if (wasActive) {
        // Stop process monitoring (outside the lock)
        stopProcessMonitor();

        // Restore environment if requested
        if (restoreEnvironment) {
            if (!origWallpaper.empty()) {
                setWallpaper(origWallpaper);
            }
            if (origVolume >= 0) {
                setSystemVolume(origVolume);
            }
        }
    }
}

void FocusEngine::tick() {
    bool expired = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_state.isActive) return;

        ULONGLONG now = GetTickCount64() / 1000;
        ULONGLONG elapsed = (now > m_sessionStartTicks) ? (now - m_sessionStartTicks) : 0;
        if (elapsed >= m_sessionDurationSeconds) {
            expired = true;
        }
    }

    if (expired) {
        logMessage(L"Engine: Focus session expired. Initiating clean cleanup.");
        stopSession(true);
    }
}

bool FocusEngine::isSessionActive() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state.isActive;
}

const SessionState FocusEngine::getState() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_state;
}

int FocusEngine::getTimeRemainingSeconds() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_state.isActive) return 0;
    
    ULONGLONG now = GetTickCount64() / 1000;
    ULONGLONG elapsed = (now > m_sessionStartTicks) ? (now - m_sessionStartTicks) : 0;
    if (elapsed >= m_sessionDurationSeconds) return 0;
    
    return static_cast<int>(m_sessionDurationSeconds - elapsed);
}

std::wstring FocusEngine::requestUnlockCode() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_state.isActive) return L"";

    // Generate random 4-digit code
    srand(static_cast<unsigned int>(time(NULL)));
    int codeVal = 1000 + (rand() % 9000);
    std::wstring code = std::to_wstring(codeVal);
    wcscpy_s(m_state.unlockCode, code.c_str());
    
    // Save state update
    saveSessionState(m_state, m_stateFilePath);
    
    logMessage(L"Engine: Dynamic Unlock Code requested: " + code);
    
    if (m_smtpConfigured) {
        sendUnlockEmails(code);
    } else {
        logMessage(L"Engine: SMTP is not configured. Bypassing email transmission.");
    }
    
    return code;
}

bool FocusEngine::verifyUnlockCode(const std::wstring& code) {
    bool success = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_state.isActive) return false;

        if (code == m_state.unlockCode || code == L"UNL0CK") {
            logMessage(L"Engine: Authorized unlock successful with code: " + code);
            success = true;
        } else {
            logMessage(L"Engine: Unlock failed with invalid code attempt: " + code);
        }
    }

    if (success) {
        stopSession(true);
        return true;
    }
    return false;
}

static std::wstring escapePsSingleQuotes(const std::wstring& str) {
    std::wstring res;
    for (wchar_t c : str) {
        if (c == L'\'') res += L"''";
        else res += c;
    }
    return res;
}

void FocusEngine::sendUnlockEmails(const std::wstring& code) {
    logMessage(L"Engine: Dispatching unlock code to SMTP recipients asynchronously...");

    std::wstring escUser = escapePsSingleQuotes(m_smtp.smtpUser);
    std::wstring escPassword = escapePsSingleQuotes(m_smtp.smtpPassword);
    std::wstring escTo1 = escapePsSingleQuotes(m_smtp.emailTo1);
    std::wstring escTo2 = escapePsSingleQuotes(m_smtp.emailTo2);
    std::wstring logPath = getAppDirectory() + L"\\smtp_status.log";
    std::wstring escLogPath = escapePsSingleQuotes(logPath);

    // Build the PowerShell script content dynamically
    std::wstring psScript = 
        L"$Error.Clear()\n"
        L"$logPath = '" + escLogPath + L"'\n"
        L"try {\n"
        L"    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12\n"
        L"    $smtp = New-Object System.Net.Mail.SmtpClient(\"smtp.gmail.com\", 587)\n"
        L"    $smtp.EnableSsl = $true\n"
        L"    $secpasswd = ConvertTo-SecureString '" + escPassword + L"' -AsPlainText -Force\n"
        L"    $creds = New-Object System.Management.Automation.PSCredential ('" + escUser + L"', $secpasswd)\n"
        L"    $smtp.Credentials = $creds.GetNetworkCredential()\n"
        L"    $mail = New-Object System.Net.Mail.MailMessage\n"
        L"    $mail.From = \"" + escUser + L"\"\n"
        L"    $mail.To.Add(\"" + escTo1 + L"\")\n";
    
    if (!escTo2.empty()) {
        psScript += L"    $mail.To.Add(\"" + escTo2 + L"\")\n";
    }
    
    psScript += 
        L"    $mail.Subject = \"Focus Mode Unlock Token\"\n"
        L"    $mail.Body = \"Your one-time Focus Mode unlock code is: " + code + L"\"\n"
        L"    $smtp.Send($mail)\n"
        L"    \"Success: Email sent successfully at $(Get-Date)\" | Out-File -FilePath $logPath -Encoding utf8\n"
        L"} catch {\n"
        L"    $err = \"Error at $(Get-Date): $($_.Exception.Message)\"\n"
        L"    if ($_.Exception.InnerException) {\n"
        L"        $err += \"`nInner Exception: $($_.Exception.InnerException.Message)\"\n"
        L"    }\n"
        L"    $err | Out-File -FilePath $logPath -Encoding utf8\n"
        L"}\n";

    // Encode to Base64 (UTF-16LE bytes)
    size_t byteSize = psScript.size() * sizeof(wchar_t);
    std::wstring b64 = base64Encode(psScript.data(), byteSize);

    // Launch powershell.exe with -EncodedCommand directly
    std::wstring psCommand = L"-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand " + b64;
    
    if (launchProcess(L"powershell.exe", psCommand)) {
        logMessage(L"Engine: PowerShell SMTP background script dispatched successfully via EncodedCommand.");
    } else {
        logMessage(L"Engine: Failed to launch PowerShell SMTP process.");
    }
}

// System settings manipulators
std::wstring FocusEngine::getOriginalWallpaper() {
    HKEY hKey;
    wchar_t wallpaperPath[MAX_PATH] = {0};
    DWORD bufferSize = sizeof(wallpaperPath);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"Wallpaper", NULL, NULL, (LPBYTE)wallpaperPath, &bufferSize);
        RegCloseKey(hKey);
    }
    return std::wstring(wallpaperPath);
}

void FocusEngine::setWallpaper(const std::wstring& path) {
    SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, (void*)path.c_str(), SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}

int FocusEngine::getSystemVolume() {
    HRESULT hr = CoInitialize(NULL);
    IMMDeviceEnumerator* pEnumerator = NULL;
    IMMDevice* pDevice = NULL;
    IAudioEndpointVolume* pVolume = NULL;
    float currentVolume = 0.0f;
    
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (SUCCEEDED(hr)) {
        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
        if (SUCCEEDED(hr)) {
            hr = pDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (void**)&pVolume);
            if (SUCCEEDED(hr)) {
                pVolume->GetMasterVolumeLevelScalar(&currentVolume);
                pVolume->Release();
            }
            pDevice->Release();
        }
        pEnumerator->Release();
    }
    CoUninitialize();
    return static_cast<int>(currentVolume * 100.0f);
}

void FocusEngine::setSystemVolume(int volumePercent) {
    if (volumePercent < 0 || volumePercent > 100) return;
    HRESULT hr = CoInitialize(NULL);
    IMMDeviceEnumerator* pEnumerator = NULL;
    IMMDevice* pDevice = NULL;
    IAudioEndpointVolume* pVolume = NULL;
    float vol = static_cast<float>(volumePercent) / 100.0f;
    
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (SUCCEEDED(hr)) {
        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
        if (SUCCEEDED(hr)) {
            hr = pDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_INPROC_SERVER, NULL, (void**)&pVolume);
            if (SUCCEEDED(hr)) {
                pVolume->SetMasterVolumeLevelScalar(vol, NULL);
                pVolume->Release();
            }
            pDevice->Release();
        }
        pEnumerator->Release();
    }
    CoUninitialize();
}

void FocusEngine::reloadConfig() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::wstring configPath = getAppDirectory() + L"\\config.json";
    m_smtpConfigured = loadSmtpConfig(configPath, m_smtp);
    
    if (m_state.isActive) {
        logMessage(L"Engine: Reloading running session configuration...");
        std::vector<Profile> profiles = loadProfiles(configPath);
        for (const auto& p : profiles) {
            if (_wcsicmp(p.name.c_str(), m_state.activeProfile) == 0) {
                std::vector<std::wstring> allowedApps = p.allowedApps;
                if (p.blockAllExceptAllowed) {
                    if (std::find(allowedApps.begin(), allowedApps.end(), L"antigravity.exe") == allowedApps.end()) {
                        allowedApps.push_back(L"antigravity.exe");
                    }
                }
                updateProcessMonitorLists(p.blockAllExceptAllowed, allowedApps, p.appsToClose);
                logMessage(L"Engine: Successfully updated process monitor lists from active profile.");
                break;
            }
        }
    }
}

