#pragma once
#include <string>
#include <vector>

// settings.json next to the executable. There is no UI: the file is written
// with every default on first run and can be edited by hand (read at startup).
struct Settings {
    // Placement, relative to the head.
    float distance = 0.9f;      // m, from the eyes to the notification
    float width = 0.34f;        // m, width of one notification
    float pitch = -17.0f;       // degrees below (negative) the gaze
    // Tag-along: the notification stays put while it is within this cone
    // around its resting place, then glides back in front of you.
    float deadzoneYaw = 12.0f;  // degrees
    float deadzonePitch = 9.0f; // degrees
    float followTime = 0.30f;   // s, smoothing of the glide (higher = lazier)

    // Look.
    std::string theme = "auto"; // auto (follows Windows) | dark | light
    float opacity = 0.8f;       // background opacity: Mica's tint opacity, the scene shows through
    float renderScale = 2.0f;   // texture pixels per Windows pixel

    // Behaviour.
    bool respectDoNotDisturb = true;
    float shortDuration = 0;    // s; 0 = Windows' "dismiss notifications after" setting
    float longDuration = 25.0f; // s, toasts sent with duration="long"
    // Reminders / alarms / calls stay on Windows until acted upon. Closing their
    // banner (X) is invisible to other apps, so by default they last
    // longDuration in VR; true keeps them until removed from Windows.
    bool keepReminders = false;
    std::vector<std::string> excludedApps; // AppUserModelIDs, or parts of them

    bool load(const std::wstring& path);
    bool save(const std::wstring& path) const;
};
