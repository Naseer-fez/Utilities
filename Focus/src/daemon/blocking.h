#pragma once
#include <vector>
#include <string>

void startProcessMonitor(bool blockAllExceptAllowed, const std::vector<std::wstring>& allowedApps, const std::vector<std::wstring>& blockedApps);
void stopProcessMonitor();
void updateProcessMonitorLists(bool blockAllExceptAllowed, const std::vector<std::wstring>& allowedApps, const std::vector<std::wstring>& blockedApps);

