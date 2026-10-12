#pragma once
// Windows 11 Mica: the desktop wallpaper, heavily blurred, given the tint's
// luminosity and then tinted. Like the real thing it is opaque and only
// depends on the wallpaper (here: the bottom-right corner, where Windows
// shows its notifications).
#include <string>

#include "gfx/canvas.h"
#include "gfx/image.h"

class Mica {
public:
    // Fills `out` (w x h) with the material. Falls back to the plain tint.
    void render(int w, int h, Color tint, float tintOpacity, bool enabled, Image& out);

private:
    bool refresh(); // reloads the wallpaper when it changed
    std::wstring path_;
    FILETIME stamp_{};
    uint32_t solid_ = 0;
    bool loaded_ = false;
    Image blurred_; // small, blurred wallpaper
};
