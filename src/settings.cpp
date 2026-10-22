#include "settings.h"

#include <string>
#include <vector>

namespace {

constexpr UINT kAllowedModifiers = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;
constexpr wchar_t kAppRegistryPath[] = L"Software\\EasyQR";
constexpr wchar_t kRunRegistryPath[] =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kAutoStartValue[] = L"EasyQR";

bool ReadDword(const wchar_t* name, DWORD* value) {
    DWORD size = sizeof(DWORD);
    const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER, kAppRegistryPath, name,
                                        RRF_RT_REG_DWORD, nullptr, value, &size);
    return status == ERROR_SUCCESS;
}

bool WriteDword(const wchar_t* name, DWORD value) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kAppRegistryPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS status =
        RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                       sizeof(value));
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
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

}  // namespace

bool IsSupportedHotkey(UINT vk) {
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return true;
    if (vk >= VK_F1 && vk <= VK_F24) return true;
    switch (vk) {
        case VK_SPACE:
        case VK_TAB:
        case VK_RETURN:
        case VK_LEFT:
        case VK_UP:
        case VK_RIGHT:
        case VK_DOWN:
        case VK_HOME:
        case VK_END:
        case VK_PRIOR:
        case VK_NEXT:
        case VK_INSERT:
        case VK_DELETE:
            return true;
        default:
            return false;
    }
}

bool IsFunctionKey(UINT vk) { return vk >= VK_F1 && vk <= VK_F24; }

bool IsModifierKey(UINT vk) {
    return vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN ||
           vk == VK_RWIN;
}

bool HotkeyBinding::IsValid() const {
    if (!IsSupportedHotkey(vk)) return false;
    if ((mods & ~kAllowedModifiers) != 0) return false;
    return mods != 0 || IsFunctionKey(vk);
}

DWORD HotkeyBinding::Pack() const {
    return ((static_cast<DWORD>(mods) & 0xFFu) << 8) | (static_cast<DWORD>(vk) & 0xFFu);
}

HotkeyBinding HotkeyBinding::Unpack(DWORD value) {
    HotkeyBinding binding;
    binding.mods = (value >> 8) & 0xFFu;
    binding.vk = value & 0xFFu;
    return binding;
}

HotkeyBinding HotkeyBinding::DefaultFullscreen() {
    HotkeyBinding binding;
    binding.mods = MOD_WIN | MOD_SHIFT;
    binding.vk = 'Q';
    return binding;
}

HotkeyBinding HotkeyBinding::DefaultRegion() {
    HotkeyBinding binding;
    binding.mods = MOD_WIN | MOD_SHIFT;
    binding.vk = 'W';
    return binding;
}

std::wstring KeyName(UINT vk) {
    if (vk >= 'A' && vk <= 'Z') return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= '0' && vk <= '9') return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= VK_F1 && vk <= VK_F24) return L"F" + std::to_wstring(vk - VK_F1 + 1);
    switch (vk) {
        case VK_SPACE:
            return L"Space";
        case VK_TAB:
            return L"Tab";
        case VK_RETURN:
            return L"Enter";
        case VK_LEFT:
            return L"Left";
        case VK_UP:
            return L"Up";
        case VK_RIGHT:
            return L"Right";
        case VK_DOWN:
            return L"Down";
        case VK_HOME:
            return L"Home";
        case VK_END:
            return L"End";
        case VK_PRIOR:
            return L"Page Up";
        case VK_NEXT:
            return L"Page Down";
        case VK_INSERT:
            return L"Insert";
        case VK_DELETE:
            return L"Delete";
        default:
            return L"?";
    }
}

std::wstring HotkeyBinding::Label() const {
    if (!IsValid()) return L"未设置";
    std::wstring text;
    auto append = [&text](const wchar_t* part) {
        if (!text.empty()) text += L" + ";
        text += part;
    };
    if (mods & MOD_WIN) append(L"Win");
    if (mods & MOD_CONTROL) append(L"Ctrl");
    if (mods & MOD_ALT) append(L"Alt");
    if (mods & MOD_SHIFT) append(L"Shift");
    append(KeyName(vk).c_str());
    return text;
}

ThemeMode LoadThemeMode() {
    DWORD value = static_cast<DWORD>(ThemeMode::System);
    if (ReadDword(L"ThemeMode", &value) && value <= 2) {
        return static_cast<ThemeMode>(value);
    }
    return ThemeMode::System;
}

void SaveThemeMode(ThemeMode mode) { WriteDword(L"ThemeMode", static_cast<DWORD>(mode)); }

bool LoadCloseToTray() {
    DWORD value = 0;
    return ReadDword(L"CloseToTray", &value) && value != 0;
}

void SaveCloseToTray(bool enabled) { WriteDword(L"CloseToTray", enabled ? 1u : 0u); }

bool LoadTranslucency() {
    DWORD value = 1;
    return !ReadDword(L"Translucency", &value) || value != 0;
}

void SaveTranslucency(bool enabled) { WriteDword(L"Translucency", enabled ? 1u : 0u); }

bool IsAutoStartEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunRegistryPath, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    const LSTATUS status = RegQueryValueExW(key, kAutoStartValue, nullptr, &type, nullptr, &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ);
}

bool SetAutoStart(bool enabled) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunRegistryPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    LSTATUS status = ERROR_SUCCESS;
    if (enabled) {
        const std::wstring executable = ExecutablePath();
        if (executable.empty()) {
            RegCloseKey(key);
            return false;
        }
        const std::wstring command = L"\"" + executable + L"\" --tray";
        status = RegSetValueExW(key, kAutoStartValue, 0, REG_SZ,
                                reinterpret_cast<const BYTE*>(command.c_str()),
                                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        status = RegDeleteValueW(key, kAutoStartValue);
        if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

HotkeyBinding LoadHotkey(bool fullscreen) {
    const HotkeyBinding fallback =
        fullscreen ? HotkeyBinding::DefaultFullscreen() : HotkeyBinding::DefaultRegion();
    DWORD value = 0;
    if (!ReadDword(fullscreen ? L"HotkeyFullscreen" : L"HotkeyRegion", &value)) return fallback;
    const HotkeyBinding binding = HotkeyBinding::Unpack(value);
    return binding.IsValid() ? binding : fallback;
}

void SaveHotkey(bool fullscreen, const HotkeyBinding& binding) {
    WriteDword(fullscreen ? L"HotkeyFullscreen" : L"HotkeyRegion", binding.Pack());
}
