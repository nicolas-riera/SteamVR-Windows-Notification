#pragma once
// Windows settings that decide whether and how a banner is shown.
#include <cstdint>
#include <string>

namespace sys {

// Do Not Disturb / Focus: 0 = off, 1 = priority only, 2 = alarms only.
int quietHoursProfile();
// Settings > Notifications master switch.
bool toastsEnabled();
// Accessibility "Dismiss notifications after" (seconds, default 5).
float messageDuration();
// System (taskbar / notification) theme.
bool lightTheme();
// Accent color as 0xRRGGBB.
uint32_t accentColor();
// Current wallpaper file ("" for a solid color), and the background color.
std::wstring wallpaperPath();
uint32_t backgroundColor();

} // namespace sys
