#include "update.h"

#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t kOwner[] = L"Northodieart";
constexpr wchar_t kRepository[] = L"EasyQR";
constexpr wchar_t kChecksumAsset[] = L"SHA256SUMS.txt";
constexpr wchar_t kPortableAsset[] = L"EasyQR.exe";
constexpr DWORD kMaxDownloadBytes = 64u * 1024u * 1024u;

std::wstring WinError(const std::wstring& action, DWORD code = GetLastError()) {
    return action + L"（错误 " + std::to_wstring(code) + L"）";
}

std::wstring ExecutablePath() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length =
            GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) return std::wstring();
        if (length < buffer.size() - 1) {
            return std::wstring(buffer.data(), length);
        }
        buffer.resize(buffer.size() * 2);
    }
}

std::wstring DirectoryName(std::wstring path) {
    const size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return path;
    path.resize(slash);
    while (!path.empty() && (path.back() == L'\\' || path.back() == L'/')) path.pop_back();
    return path;
}

std::wstring TempDirectory() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
        if (length == 0) return std::wstring();
        if (length < buffer.size()) {
            std::wstring result(buffer.data(), length);
            while (!result.empty() && (result.back() == L'\\' || result.back() == L'/')) {
                result.pop_back();
            }
            return result + L"\\EasyQR";
        }
        buffer.resize(buffer.size() * 2);
    }
}

bool ReadFileBytes(const std::wstring& path, std::vector<uint8_t>* data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0) {
        CloseHandle(file);
        return false;
    }
    data->resize(static_cast<size_t>(size.QuadPart));
    size_t offset = 0;
    while (offset < data->size()) {
        DWORD read = 0;
        const DWORD chunk =
            static_cast<DWORD>(std::min<size_t>(data->size() - offset, 1u << 20));
        if (!ReadFile(file, data->data() + offset, chunk, &read, nullptr) || read == 0) {
            CloseHandle(file);
            return false;
        }
        offset += read;
    }
    CloseHandle(file);
    return true;
}

bool WriteFileBytes(const std::wstring& path, const std::vector<uint8_t>& data) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    size_t offset = 0;
    bool ok = true;
    while (offset < data.size()) {
        DWORD written = 0;
        const DWORD chunk =
            static_cast<DWORD>(std::min<size_t>(data.size() - offset, 1u << 20));
        if (!WriteFile(file, data.data() + offset, chunk, &written, nullptr) ||
            written != chunk) {
            ok = false;
            break;
        }
        offset += written;
    }
    CloseHandle(file);
    return ok;
}

bool IsSafeTag(const std::wstring& tag) {
    if (tag.empty() || tag.size() > 64) return false;
    for (wchar_t c : tag) {
        const bool safe = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                          (c >= L'0' && c <= L'9') || c == L'.' || c == L'-' || c == L'_';
        if (!safe) return false;
    }
    return true;
}

bool ExtractVersion(const std::string& text, std::wstring* version) {
    for (size_t start = 0; start < text.size(); ++start) {
        if (text[start] < '0' || text[start] > '9') continue;
        size_t end = start;
        bool dot = false;
        while (end < text.size()) {
            const char c = text[end];
            if (c >= '0' && c <= '9') {
                ++end;
            } else if (c == '.') {
                dot = true;
                ++end;
            } else {
                break;
            }
        }
        if (!dot) continue;
        std::wstring candidate(text.begin() + static_cast<std::ptrdiff_t>(start),
                               text.begin() + static_cast<std::ptrdiff_t>(end));
        while (!candidate.empty() && candidate.back() == L'.') candidate.pop_back();
        if (IsValidVersion(candidate)) {
            *version = candidate;
            return true;
        }
    }
    return false;
}

bool IsJsonString(const std::string& body, const char* key, std::string* value) {
    const std::string pattern = std::string("\"") + key + "\"";
    size_t position = body.find(pattern);
    if (position == std::string::npos) return false;
    position = body.find(':', position + pattern.size());
    if (position == std::string::npos) return false;
    ++position;
    while (position < body.size() &&
           (body[position] == ' ' || body[position] == '\t' || body[position] == '\r' ||
            body[position] == '\n')) {
        ++position;
    }
    if (position >= body.size() || body[position] != '"') return false;
    ++position;
    std::string result;
    while (position < body.size()) {
        const char c = body[position++];
        if (c == '"') {
            *value = std::move(result);
            return true;
        }
        if (c == '\\' && position < body.size()) {
            const char escaped = body[position++];
            if (escaped == 'n' || escaped == 'r' || escaped == 't') {
                result.push_back(' ');
            } else {
                result.push_back(escaped);
            }
        } else {
            result.push_back(c);
        }
    }
    return false;
}

struct HttpResponse {
    DWORD status = 0;
    std::vector<uint8_t> body;
    std::wstring error;
};

bool HttpGet(const std::wstring& url, HttpResponse* response) {
    response->status = 0;
    response->body.clear();
    response->error.clear();

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        response->error = WinError(L"更新地址解析失败");
        return false;
    }

    const std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    const bool secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    const INTERNET_PORT port = components.nPort;

    HINTERNET session = WinHttpOpen(L"EasyQR-Updater/1.0",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        session = WinHttpOpen(L"EasyQR-Updater/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (!session) {
        response->error = WinError(L"无法初始化网络连接");
        return false;
    }
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 15000);

    bool ok = false;
    HINTERNET connection = WinHttpConnect(session, host.c_str(), port, 0);
    HINTERNET request = nullptr;
    if (connection) {
        request = WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                     secure ? WINHTTP_FLAG_SECURE : 0);
    }
    if (!request) {
        response->error = WinError(L"无法建立更新请求");
    } else {
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        if (secure) {
            WinHttpSetOption(request, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                             sizeof(protocols));
        }
        DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy,
                         sizeof(redirectPolicy));
        std::wstring headers = L"User-Agent: EasyQR-Updater/1.0\r\n";
        if (host == L"api.github.com") {
            headers += L"Accept: application/vnd.github+json\r\n"
                       L"X-GitHub-Api-Version: 2022-11-28\r\n";
        }
        if (WinHttpSendRequest(request, headers.c_str(), static_cast<DWORD>(-1),
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD size = sizeof(response->status);
            if (!WinHttpQueryHeaders(request,
                                     WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                     WINHTTP_HEADER_NAME_BY_INDEX, &response->status, &size,
                                     WINHTTP_NO_HEADER_INDEX)) {
                response->error = WinError(L"无法读取服务器响应");
            } else {
                ok = true;
                for (;;) {
                    DWORD available = 0;
                    if (!WinHttpQueryDataAvailable(request, &available)) {
                        response->error = WinError(L"接收更新数据失败");
                        ok = false;
                        break;
                    }
                    if (available == 0) break;
                    if (response->body.size() + available > kMaxDownloadBytes) {
                        response->error = L"更新文件过大，已停止下载";
                        ok = false;
                        break;
                    }
                    const size_t offset = response->body.size();
                    response->body.resize(offset + available);
                    DWORD read = 0;
                    if (!WinHttpReadData(request, response->body.data() + offset, available,
                                         &read)) {
                        response->error = WinError(L"接收更新数据失败");
                        ok = false;
                        break;
                    }
                    response->body.resize(offset + read);
                }
            }
        } else {
            response->error = WinError(L"无法连接 GitHub，请检查网络或代理");
        }
    }

    if (request) WinHttpCloseHandle(request);
    if (connection) WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return ok;
}

bool Sha256File(const std::wstring& path, std::wstring* digest, std::wstring* error) {
    HCRYPTPROV provider = 0;
    HCRYPTHASH hash = 0;
    HANDLE file = INVALID_HANDLE_VALUE;
    bool ok = false;
    if (!CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES,
                              CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        *error = WinError(L"无法初始化 SHA-256 校验");
        return false;
    }
    if (!CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash)) {
        *error = WinError(L"无法创建 SHA-256 校验");
        CryptReleaseContext(provider, 0);
        return false;
    }
    file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        *error = L"无法读取下载的更新文件";
    } else {
        std::vector<uint8_t> buffer(64 * 1024);
        ok = true;
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
                *error = L"读取更新文件失败";
                ok = false;
                break;
            }
            if (read == 0) break;
            if (!CryptHashData(hash, buffer.data(), read, 0)) {
                *error = L"计算 SHA-256 失败";
                ok = false;
                break;
            }
        }
    }

    if (ok) {
        BYTE bytes[32]{};
        DWORD size = sizeof(bytes);
        if (!CryptGetHashParam(hash, HP_HASHVAL, bytes, &size, 0)) {
            *error = L"计算 SHA-256 失败";
            ok = false;
        } else {
            static const wchar_t kHex[] = L"0123456789abcdef";
            digest->clear();
            for (DWORD i = 0; i < size; ++i) {
                digest->push_back(kHex[bytes[i] >> 4]);
                digest->push_back(kHex[bytes[i] & 0xF]);
            }
        }
    }

    if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    if (hash) CryptDestroyHash(hash);
    CryptReleaseContext(provider, 0);
    return ok;
}

bool VerifyChecksum(const std::wstring& filePath, const std::wstring& checksumPath,
                    const std::wstring& assetName, std::wstring* error) {
    std::vector<uint8_t> bytes;
    if (!ReadFileBytes(checksumPath, &bytes)) {
        *error = L"无法读取更新校验文件";
        return false;
    }
    const std::string text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::string expected;
    size_t lineStart = 0;
    while (lineStart < text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = text.size();
        std::string line = text.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t space = line.find_first_of(" \t");
        if (space != std::string::npos) {
            std::string checksum = line.substr(0, space);
            size_t nameStart = line.find_first_not_of(" \t", space);
            if (nameStart != std::string::npos) {
                if (line[nameStart] == '*') ++nameStart;
                const std::string name = line.substr(nameStart);
                const size_t slash = name.find_last_of("\\/");
                const std::string baseName =
                    slash == std::string::npos ? name : name.substr(slash + 1);
                if (baseName == std::string(assetName.begin(), assetName.end()) &&
                    checksum.size() == 64) {
                    expected = checksum;
                    break;
                }
            }
        }
        lineStart = lineEnd + 1;
    }

    if (expected.empty()) {
        *error = L"更新校验文件中缺少目标文件";
        return false;
    }

    std::wstring actual;
    if (!Sha256File(filePath, &actual, error)) return false;
    std::string actualNarrow(actual.begin(), actual.end());
    std::transform(expected.begin(), expected.end(), expected.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (actualNarrow != expected) {
        *error = L"SHA-256 校验失败，更新文件已丢弃";
        DeleteFileW(filePath.c_str());
        return false;
    }
    return true;
}

std::wstring NormalizeVersion(std::wstring version) {
    if (!version.empty() && (version[0] == L'v' || version[0] == L'V')) {
        version.erase(version.begin());
    }
    return version;
}

std::wstring AssetUrl(const std::wstring& tag, const std::wstring& asset) {
    return L"https://github.com/" + std::wstring(kOwner) + L"/" + kRepository +
           L"/releases/download/" + tag + L"/" + asset;
}

}  // namespace

bool IsValidVersion(const std::wstring& version) {
    if (version.empty() || version.size() > 32) return false;
    bool digitSeen = false;
    bool dotAllowed = false;
    for (wchar_t c : version) {
        if (c >= L'0' && c <= L'9') {
            digitSeen = true;
            dotAllowed = true;
        } else if (c == L'.') {
            if (!dotAllowed) return false;
            dotAllowed = false;
            digitSeen = false;
        } else {
            return false;
        }
    }
    return digitSeen;
}

int CompareVersions(const std::wstring& left, const std::wstring& right) {
    auto parse = [](const std::wstring& value) {
        std::vector<int> parts;
        int current = 0;
        bool any = false;
        for (wchar_t c : value) {
            if (c >= L'0' && c <= L'9') {
                current = current * 10 + static_cast<int>(c - L'0');
                any = true;
            } else if (c == L'.') {
                parts.push_back(current);
                current = 0;
                any = false;
            } else {
                return std::vector<int>{};
            }
        }
        if (any) parts.push_back(current);
        return parts;
    };
    std::vector<int> a = parse(NormalizeVersion(left));
    std::vector<int> b = parse(NormalizeVersion(right));
    const size_t count = std::max(a.size(), b.size());
    a.resize(count, 0);
    b.resize(count, 0);
    for (size_t i = 0; i < count; ++i) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

UpdateCheckResult CheckForUpdate(const std::wstring& currentVersion) {
    UpdateCheckResult result;
    const std::wstring api = L"https://api.github.com/repos/" + std::wstring(kOwner) + L"/" +
                             kRepository + L"/releases/latest";
    HttpResponse response;
    if (!HttpGet(api, &response)) {
        result.error = response.error.empty() ? L"检查更新失败" : response.error;
        return result;
    }
    if (response.status == 404) {
        result.error = L"GitHub 上还没有可用的正式版本";
        return result;
    }
    if (response.status == 403) {
        result.error = L"GitHub 请求受限，请稍后再试";
        return result;
    }
    if (response.status != 200) {
        result.error = L"GitHub 返回 HTTP " + std::to_wstring(response.status);
        return result;
    }

    const std::string body(response.body.begin(), response.body.end());
    std::string tag;
    if (!IsJsonString(body, "tag_name", &tag) || tag.empty()) {
        result.error = L"无法解析 GitHub 版本信息";
        return result;
    }
    const std::wstring wideTag(tag.begin(), tag.end());
    if (!IsSafeTag(wideTag)) {
        result.error = L"GitHub 发布标签格式无法识别";
        return result;
    }

    result.tag = wideTag;
    std::wstring version = NormalizeVersion(wideTag);
    if (!IsValidVersion(version)) {
        std::string releaseName;
        if (IsJsonString(body, "name", &releaseName)) {
            ExtractVersion(releaseName, &version);
        }
    }
    if (!IsValidVersion(version)) {
        ExtractVersion(tag, &version);
    }
    if (!IsValidVersion(version)) {
        const std::string marker = "\"name\":\"";
        size_t position = 0;
        while (!IsValidVersion(version) &&
               (position = body.find(marker, position)) != std::string::npos) {
            position += marker.size();
            const size_t end = body.find('"', position);
            if (end == std::string::npos) break;
            ExtractVersion(body.substr(position, end - position), &version);
            position = end + 1;
        }
    }
    if (!IsValidVersion(version)) {
        result.error = L"无法从 GitHub 发布信息中识别版本号";
        return result;
    }

    result.version = version;
    result.ok = true;
    result.available = IsValidVersion(currentVersion) &&
                       CompareVersions(result.version, currentVersion) > 0;
    return result;
}

UpdatePrepareResult PrepareUpdate(const std::wstring& version, const std::wstring& tag) {
    UpdatePrepareResult result;
    if (!IsValidVersion(version) || !IsSafeTag(tag)) {
        result.error = L"更新版本信息无效";
        return result;
    }

    const bool installed = IsInstalledCopy();
    const std::wstring asset = installed ? L"EasyQR-" + version + L"-x64-setup.exe"
                                        : std::wstring(kPortableAsset);
    const std::wstring directory = TempDirectory();
    if (directory.empty() || !CreateDirectoryW(directory.c_str(), nullptr)) {
        const DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            result.error = L"无法创建更新临时目录";
            return result;
        }
    }

    const std::wstring checksumPath = directory + L"\\" + kChecksumAsset;
    const std::wstring filePath = directory + L"\\" + asset;
    DeleteFileW(checksumPath.c_str());
    DeleteFileW(filePath.c_str());

    HttpResponse checksum;
    if (!HttpGet(AssetUrl(tag, kChecksumAsset), &checksum) || checksum.status != 200) {
        result.error = checksum.error.empty()
                           ? L"更新校验文件下载失败"
                           : checksum.error;
        return result;
    }
    if (!WriteFileBytes(checksumPath, checksum.body)) {
        result.error = WinError(L"无法写入更新校验文件");
        return result;
    }

    HttpResponse binary;
    if (!HttpGet(AssetUrl(tag, asset), &binary) || binary.status != 200) {
        result.error = binary.status == 404
                           ? L"发布中缺少更新文件：" + asset
                           : (binary.error.empty() ? L"更新文件下载失败" : binary.error);
        DeleteFileW(checksumPath.c_str());
        return result;
    }
    if (!WriteFileBytes(filePath, binary.body)) {
        result.error = WinError(L"无法写入更新文件");
        DeleteFileW(checksumPath.c_str());
        DeleteFileW(filePath.c_str());
        return result;
    }

    std::wstring verifyError;
    if (!VerifyChecksum(filePath, checksumPath, asset, &verifyError)) {
        result.error = verifyError;
        DeleteFileW(checksumPath.c_str());
        DeleteFileW(filePath.c_str());
        return result;
    }
    DeleteFileW(checksumPath.c_str());

    result.ok = true;
    result.installer = installed;
    result.filePath = filePath;
    return result;
}

bool IsInstalledCopy() {
    const std::wstring executable = ExecutablePath();
    if (executable.empty()) return false;
    const std::wstring directory = DirectoryName(executable);

    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\EasyQR", 0,
                      KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        return false;
    }
    wchar_t installDir[32768]{};
    DWORD size = sizeof(installDir);
    DWORD type = 0;
    const LSTATUS status = RegQueryValueExW(key, L"InstallDir", nullptr, &type,
                                            reinterpret_cast<LPBYTE>(installDir), &size);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) return false;

    wchar_t expanded[32768]{};
    if (!ExpandEnvironmentStringsW(installDir, expanded, ARRAYSIZE(expanded))) return false;
    std::wstring installed(expanded);
    while (!installed.empty() &&
           (installed.back() == L'\\' || installed.back() == L'/')) {
        installed.pop_back();
    }
    return _wcsicmp(directory.c_str(), installed.c_str()) == 0;
}

bool StartInstaller(const std::wstring& installerPath, std::wstring* error) {
    const std::wstring directory = DirectoryName(installerPath);
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas";
    info.lpFile = installerPath.c_str();
    info.lpParameters = L"/S /EASYQRUPDATE";
    info.lpDirectory = directory.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        const DWORD code = GetLastError();
        *error = code == ERROR_CANCELLED ? L"已取消管理员授权，未执行更新"
                                         : WinError(L"无法启动更新程序", code);
        return false;
    }
    if (info.hProcess) CloseHandle(info.hProcess);
    return true;
}

void CleanupUpdateCache() {
    const std::wstring directory = TempDirectory();
    if (directory.empty()) return;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((directory + L"\\*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            DeleteFileW((directory + L"\\" + data.cFileName).c_str());
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    RemoveDirectoryW(directory.c_str());
}

bool ApplyPortableUpdate(const std::wstring& newExecutable, std::wstring* error) {
    const std::wstring current = ExecutablePath();
    if (current.empty()) {
        *error = L"无法确定当前程序位置";
        return false;
    }
    const std::wstring backup = current + L".old";
    DeleteFileW(backup.c_str());
    if (!MoveFileExW(current.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        *error = WinError(L"无法替换当前程序，请检查目录写入权限");
        return false;
    }
    if (!CopyFileW(newExecutable.c_str(), current.c_str(), FALSE)) {
        *error = WinError(L"无法写入新版本程序");
        MoveFileExW(backup.c_str(), current.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }

    std::wstring commandLine = L"\"" + current + L"\" --updated";
    std::vector<wchar_t> commandBuffer(commandLine.begin(), commandLine.end());
    commandBuffer.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const std::wstring workingDirectory = DirectoryName(current);
    if (!CreateProcessW(current.c_str(), commandBuffer.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_PROCESS_GROUP, nullptr, workingDirectory.c_str(), &startup,
                        &process)) {
        *error = WinError(L"新版本已就位，但无法自动启动，请手动运行 EasyQR");
        DeleteFileW(current.c_str());
        MoveFileExW(backup.c_str(), current.c_str(), MOVEFILE_REPLACE_EXISTING);
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    DeleteFileW(newExecutable.c_str());
    return true;
}
