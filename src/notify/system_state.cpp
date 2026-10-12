#include "notify/system_state.h"

#include <windows.h>

namespace {

DWORD regDword(HKEY root, const wchar_t* key, const wchar_t* value, DWORD def) {
    DWORD v = 0, size = sizeof(v);
    if (RegGetValueW(root, key, value, RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS) return def;
    return v;
}

// Windows Notification Facility: the shell publishes Focus / Do Not Disturb there.
extern "C" LONG NTAPI NtQueryWnfStateData(const uint64_t* stateName, const void* typeId, const void* scope, ULONG* changeStamp,
                                          void* buffer, ULONG* bufferSize);
constexpr uint64_t WNF_SHEL_QUIETHOURS_ACTIVE_PROFILE_CHANGED = 0x0D83063EA3BF1C75ull;

} // namespace

namespace sys {

int quietHoursProfile() {
    uint64_t name = WNF_SHEL_QUIETHOURS_ACTIVE_PROFILE_CHANGED;
    ULONG stamp = 0, size = sizeof(DWORD);
    DWORD value = 0;
    if (NtQueryWnfStateData(&name, nullptr, nullptr, &stamp, &value, &size) < 0 || size < sizeof(DWORD)) return 0;
    return int(value);
}

bool toastsEnabled() {
    return regDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\PushNotifications", L"ToastEnabled", 1) != 0 &&
           regDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Notifications\\Settings",
                    L"NOC_GLOBAL_SETTING_TOASTS_ENABLED", 1) != 0;
}

float messageDuration() {
    DWORD s = regDword(HKEY_CURRENT_USER, L"Control Panel\\Accessibility", L"MessageDuration", 5);
    return s >= 1 && s <= 600 ? float(s) : 5.0f;
}

bool lightTheme() {
    return regDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    L"SystemUsesLightTheme", 0) != 0;
}

uint32_t accentColor() {
    // Stored as 0xAABBGGRR.
    DWORD c = regDword(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", 0xFFD47800);
    return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
}

std::wstring wallpaperPath() {
    wchar_t buf[MAX_PATH * 2] = {};
    if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER, DWORD(std::size(buf)), buf, 0)) return {};
    return buf;
}

uint32_t backgroundColor() {
    DWORD c = GetSysColor(COLOR_BACKGROUND); // 0x00BBGGRR
    return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
}

} // namespace sys
