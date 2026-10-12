#pragma once
#include <string>

constexpr const char* kAppKey = "steamvr.windows.notification";

// Normal mode: SteamVR overlay app, no window, no tray icon, no dashboard tab.
int runOverlay();
// Renders sample notifications to PNG files (no VR needed) + icon.png.
int runSnapshot(const std::wstring& outDir);
// Removes the SteamVR registration (autostart).
int runUnregister();
