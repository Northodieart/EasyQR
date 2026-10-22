#pragma once

#include "common.h"

enum class ThemeMode { System = 0, Light = 1, Dark = 2 };

enum class GlassMode { None = 0, Static = 1, Dynamic = 2 };

struct HotkeyBinding {
    UINT mods = 0;
    UINT vk = 0;

    bool IsValid() const;
    DWORD Pack() const;
    static HotkeyBinding Unpack(DWORD value);
    static HotkeyBinding DefaultFullscreen();
    static HotkeyBinding DefaultRegion();
    std::wstring Label() const;
};

bool IsSupportedHotkey(UINT vk);
bool IsFunctionKey(UINT vk);
bool IsModifierKey(UINT vk);
std::wstring KeyName(UINT vk);

ThemeMode LoadThemeMode();
void SaveThemeMode(ThemeMode mode);
bool LoadCloseToTray();
void SaveCloseToTray(bool enabled);
GlassMode LoadGlassMode();
void SaveGlassMode(GlassMode mode);
std::wstring LoadPendingUpdateVersion();
void SavePendingUpdateVersion(const std::wstring& version);
void ClearPendingUpdateVersion();
bool IsAutoStartEnabled();
bool SetAutoStart(bool enabled);
HotkeyBinding LoadHotkey(bool fullscreen);
void SaveHotkey(bool fullscreen, const HotkeyBinding& binding);
