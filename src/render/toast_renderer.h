#pragma once
// Paints a toast like Windows 11's notification banner: header with the app
// icon and name, optional hero image, "photo" (app logo override), title and
// body, progress bars, inline image, inputs and buttons.
// Two layers, shown as two overlays: the content (sharp, drawn once) and the
// Mica backdrop (made from the VR scene behind, low resolution, refreshed as
// the scene changes) with the shadow.
#include <string>

#include "gfx/canvas.h"
#include "gfx/image.h"
#include "notify/toast.h"
#include "vr/mirror_sampler.h"

class FontSet;

struct ToastVisual {
    Toast toast;
    std::string appName;
    Image appIcon, logo, hero, inlineImage; // already decoded (may be empty)
    std::string footer;                     // hint shown under the content ("" = none)
};

struct Theme {
    bool light = false;
    Color tint, text, text2, stroke, control, controlStroke, accent, track;
    float tintOpacity = 0.8f; // Mica's
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

    explicit ToastRenderer(FontSet& fonts) : fonts_(fonts) {}
    // Content on a transparent background (resizes `out` to even sizes; the
    // panel is inset by kShadowPad * scale). Returns the panel height (units).
    float render(const ToastVisual& v, const Theme& theme, float scale, Canvas& out);
    // Shadow + Mica panel, at `scale` into a w x h canvas (same layout as the
    // content). Without a field: the tint at `fallbackOpacity`.
    void renderBackdrop(const MicaField* field, const Theme& theme, float panelH, float scale, int w, int h, float fallbackOpacity,
                        Canvas& out);

private:
    float layout(const ToastVisual& v, Canvas* c, const Theme& th, float s, float originX, float originY);
    FontSet& fonts_;
};
