#include "render/mica.h"

#include <windows.h>

#include <algorithm>
#include <cmath>

#include "core/log.h"
#include "notify/system_state.h"

namespace {

constexpr int kSmallWidth = 192; // wallpaper working size
constexpr int kBlurRadius = 5;   // box radius at that size, 3 passes ~ Gaussian

void boxBlur(Image& img, int r) {
    std::vector<uint8_t> tmp(img.px.size());
    auto pass = [&](const uint8_t* src, uint8_t* dst, int w, int h, int sx, int sy) {
        // Blur along one axis: sx/sy are the steps (in pixels) along / across it.
        for (int line = 0; line < h; ++line) {
            for (int c = 0; c < 3; ++c) {
                int sum = 0;
                auto at = [&](int i) { i = std::clamp(i, 0, w - 1); return int(src[(size_t(line) * sy + size_t(i) * sx) * 4 + c]); };
                for (int i = -r; i <= r; ++i) sum += at(i);
                for (int i = 0; i < w; ++i) {
                    dst[(size_t(line) * sy + size_t(i) * sx) * 4 + c] = uint8_t(sum / (2 * r + 1));
                    sum += at(i + r + 1) - at(i - r);
                }
            }
            for (int i = 0; i < w; ++i) dst[(size_t(line) * sy + size_t(i) * sx) * 4 + 3] = 255;
        }
    };
    for (int k = 0; k < 3; ++k) {
        pass(img.px.data(), tmp.data(), img.w, img.h, 1, img.w); // rows
        pass(tmp.data(), img.px.data(), img.h, img.w, img.w, 1); // columns
    }
}

constexpr float kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

float lum(float r, float g, float b) { return 0.30f * r + 0.59f * g + 0.11f * b; }

// W3C "luminosity" blend: the backdrop's hue and saturation with `l`.
void setLum(float& r, float& g, float& b, float l) {
    float d = l - lum(r, g, b);
    r += d; g += d; b += d;
    float L = lum(r, g, b), n = std::min({r, g, b}), x = std::max({r, g, b});
    if (n < 0) { float k = L / std::max(L - n, 1e-4f); r = L + (r - L) * k; g = L + (g - L) * k; b = L + (b - L) * k; }
    if (x > 1) { float k = (1 - L) / std::max(x - L, 1e-4f); r = L + (r - L) * k; g = L + (g - L) * k; b = L + (b - L) * k; }
}

} // namespace

bool Mica::refresh() {
    std::wstring path = sys::wallpaperPath();
    uint32_t solid = sys::backgroundColor();
    FILETIME stamp{};
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!path.empty() && GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) stamp = fa.ftLastWriteTime;
    if (loaded_ && path == path_ && solid == solid_ && CompareFileTime(&stamp, &stamp_) == 0) return !blurred_.empty();
    path_ = path;
    stamp_ = stamp;
    solid_ = solid;
    loaded_ = true;
    blurred_ = Image{};
    if (!path.empty() && loadImage(path, kSmallWidth, kSmallWidth, blurred_)) {
        boxBlur(blurred_, kBlurRadius);
        LOG("mica: wallpaper %dx%d", blurred_.w, blurred_.h);
        return true;
    }
    // Solid color desktop.
    blurred_.w = blurred_.h = 1;
    blurred_.px = {uint8_t(solid >> 16), uint8_t(solid >> 8), uint8_t(solid), 255};
    return true;
}

void Mica::render(int w, int h, Color tint, float tintOpacity, bool enabled, Image& out) {
    out.w = w;
    out.h = h;
    out.px.resize(size_t(w) * h * 4);
    if (!enabled || !refresh()) {
        for (size_t i = 0; i < out.px.size(); i += 4) {
            out.px[i] = tint.r; out.px[i + 1] = tint.g; out.px[i + 2] = tint.b; out.px[i + 3] = 255;
        }
        return;
    }
    // Region of the wallpaper behind a toast: bottom-right corner of the
    // screen, about 364 px wide on a 1920 px screen.
    const Image& src = blurred_;
    float regW = src.w * 0.20f, regH = regW * float(h) / float(w);
    regH = std::min(regH, src.h * 0.9f);
    float rx = src.w * 0.985f - regW, ry = src.h * 0.94f - regH;
    rx = std::max(0.0f, rx);
    ry = std::max(0.0f, ry);
    const float tr = tint.r / 255.0f, tg = tint.g / 255.0f, tb = tint.b / 255.0f, tl = lum(tr, tg, tb);
    for (int y = 0; y < h; ++y) {
        float fy = std::clamp(ry + (y + 0.5f) * regH / h - 0.5f, 0.0f, float(src.h - 1));
        int iy = int(fy), iy1 = std::min(iy + 1, src.h - 1);
        float ty = fy - iy;
        for (int x = 0; x < w; ++x) {
            float fx = std::clamp(rx + (x + 0.5f) * regW / w - 0.5f, 0.0f, float(src.w - 1));
            int ix = int(fx), ix1 = std::min(ix + 1, src.w - 1);
            float tx = fx - ix;
            float c[3];
            for (int k = 0; k < 3; ++k) {
                float a = src.px[(size_t(iy) * src.w + ix) * 4 + k], b = src.px[(size_t(iy) * src.w + ix1) * 4 + k];
                float d = src.px[(size_t(iy1) * src.w + ix) * 4 + k], e = src.px[(size_t(iy1) * src.w + ix1) * 4 + k];
                c[k] = ((a + (b - a) * tx) * (1 - ty) + (d + (e - d) * tx) * ty) / 255.0f;
            }
            setLum(c[0], c[1], c[2], tl);
            uint8_t* p = &out.px[(size_t(y) * w + x) * 4];
            // Ordered dither: the smooth gradients would band in 8 bits.
            const float dither = kBayer[(y & 3) * 4 + (x & 3)] / 16.0f;
            p[0] = uint8_t(std::clamp(c[0] + (tr - c[0]) * tintOpacity, 0.0f, 1.0f) * 254.0f + dither);
            p[1] = uint8_t(std::clamp(c[1] + (tg - c[1]) * tintOpacity, 0.0f, 1.0f) * 254.0f + dither);
            p[2] = uint8_t(std::clamp(c[2] + (tb - c[2]) * tintOpacity, 0.0f, 1.0f) * 254.0f + dither);
            p[3] = 255;
        }
    }
}
