#pragma once
// Paints a toast like Windows 11's notification banner: header with the app
// icon and name, optional hero image, "photo" (app logo override), title and
// body, progress bars, inline image, inputs and buttons, on Mica.
#include <string>

#include "gfx/canvas.h"
#include "gfx/image.h"
#include "notify/toast.h"

class FontSet;
class Mica;

struct ToastVisual {
    Toast toast;
    std::string appName;
    Image appIcon, logo, hero, inlineImage; // already decoded (may be empty)
};

struct Theme {
    bool light = false;
    Color tint, text, text2, stroke, control, controlStroke, accent, track;
    float tintOpacity = 0.8f;
    static Theme make(bool light, uint32_t accentRgb);
};

class ToastRenderer {
public:
    // Windows layout units (1 unit = 1 px at 100% scaling).
    static constexpr float kWidth = 364.0f;
    static constexpr float kShadowPad = 20.0f; // transparent margin for the shadow
    // Pixel sizes images should be decoded at, for a given render scale.
    static int logoPx(float scale) { return int(48 * scale); }
    static int heroMaxPx(float scale) { return int((kWidth - 32) * scale); }
    static int iconPx(float scale) { return int(16 * scale); }

    ToastRenderer(FontSet& fonts, Mica& mica) : fonts_(fonts), mica_(mica) {}
    // Renders into `out` (resized). The panel occupies the canvas minus
    // kShadowPad * scale on each side.
    void render(const ToastVisual& v, const Theme& theme, float scale, bool mica, Canvas& out);

private:
    float layout(const ToastVisual& v, Canvas* c, const Theme& th, float s, float originX, float originY);
    FontSet& fonts_;
    Mica& mica_;
    Image backdrop_;
};
