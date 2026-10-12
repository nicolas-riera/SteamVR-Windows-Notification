#pragma once
// Application name / icon for a notification sender, and resolution of the
// image URIs found in toast payloads.
#include <map>
#include <string>

#include "gfx/image.h"

class WpnStore;

struct AppInfo {
    std::string name;
    Image icon; // empty if none was found
};

class AppDirectory {
public:
    // Cached per AUMID; iconPx is the size the header draws the icon at.
    const AppInfo& lookup(const std::string& aumid, WpnStore& store, int iconPx);

private:
    std::map<std::string, AppInfo> cache_;
};

bool isRemoteUri(const std::string& src);
// file:///, ms-appx:///, ms-appdata:///, plain paths -> local file ("" if none).
std::wstring resolveLocalImage(const std::string& src, const std::string& aumid);
