#pragma once

#include <string>

struct UpdateCheckResult {
    bool ok = false;
    bool available = false;
    std::wstring version;
    std::wstring tag;
    std::wstring error;
};

struct UpdatePrepareResult {
    bool ok = false;
    bool installer = false;
    std::wstring filePath;
    std::wstring error;
};

UpdateCheckResult CheckForUpdate(const std::wstring& currentVersion);
UpdatePrepareResult PrepareUpdate(const std::wstring& version, const std::wstring& tag);
bool IsInstalledCopy();
bool StartInstaller(const std::wstring& installerPath, std::wstring* error);
bool ApplyPortableUpdate(const std::wstring& newExecutable, std::wstring* error);
void CleanupUpdateCache();
int CompareVersions(const std::wstring& left, const std::wstring& right);
bool IsValidVersion(const std::wstring& version);
