#include "gfx/canvas.h"

#include <cmath>
#include <cstring>

#include "gfx/image.h"

void Canvas::resize(int w, int h) {
    w_ = w;
    h_ = h;
    px_.assign(size_t(w) * size_t(h) * 4, 0);
}

void Canvas::clear(Color c) {
    if (c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0) {
        std::memset(px_.data(), 0, px_.size());
        return;
    }
    for (size_t i = 0; i < px_.size(); i += 4) {
        px_[i] = c.r; px_[i + 1] = c.g; px_[i + 2] = c.b; px_[i + 3] = c.a;
    }
}

// Full-coverage run of pixels: the hot path (panel backgrounds, cards).
void Canvas::span(int x0, int x1, int y, Color c) {
    uint8_t* p = &px_[(size_t(y) * size_t(w_) + size_t(x0)) * 4];
    if (c.a == 255) {
        for (int x = x0; x < x1; ++x, p += 4) { p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 255; }
        return;
    }
    const uint32_t a = c.a, ia = 255 - a;
    for (int x = x0; x < x1; ++x, p += 4) {
        if (p[3] == 0) { p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = c.a; continue; }
        if (p[3] != 255) { blend(x, y, c, 1.0f); continue; }
        p[0] = uint8_t((c.r * a + p[0] * ia + 127) / 255);
        p[1] = uint8_t((c.g * a + p[1] * ia + 127) / 255);
        p[2] = uint8_t((c.b * a + p[2] * ia + 127) / 255);
    }
}

void Canvas::blend(int x, int y, Color c, float cov) {
    uint8_t* p = &px_[(size_t(y) * size_t(w_) + size_t(x)) * 4];
    float sa = c.a * (1.0f / 255.0f) * cov;
    if (sa >= 0.998f) { p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 255; return; }
    if (sa <= 0.0f) return;
    if (p[3] == 255) { // opaque destination: cheap lerp
        float ia = 1.0f - sa;
        p[0] = uint8_t(c.r * sa + p[0] * ia + 0.5f);
        p[1] = uint8_t(c.g * sa + p[1] * ia + 0.5f);
        p[2] = uint8_t(c.b * sa + p[2] * ia + 0.5f);
        return;
    }
    float da = p[3] * (1.0f / 255.0f);
    float k = da * (1.0f - sa);
    float oa = sa + k;
    float inv = 1.0f / oa;
    p[0] = uint8_t((c.r * sa + p[0] * k) * inv + 0.5f);
    p[1] = uint8_t((c.g * sa + p[1] * k) * inv + 0.5f);
    p[2] = uint8_t((c.b * sa + p[2] * k) * inv + 0.5f);
    p[3] = uint8_t(oa * 255.0f + 0.5f);
}

void Canvas::fillRect(Rect r, Color c) {
    int x0 = std::max(0, int(std::lround(r.x))), y0 = std::max(0, int(std::lround(r.y)));
    int x1 = std::min(w_, int(std::lround(r.x + r.w))), y1 = std::min(h_, int(std::lround(r.y + r.h)));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) blend(x, y, c, 1.0f);
}

void Canvas::shape(Rect r, float rad, float th, Color c) {
    if (r.w <= 0 || r.h <= 0 || c.a == 0) return;
    rad = std::min(rad, std::min(r.w, r.h) * 0.5f);
    int x0 = std::max(0, int(std::floor(r.x))), y0 = std::max(0, int(std::floor(r.y)));
    int x1 = std::min(w_, int(std::ceil(r.x + r.w))), y1 = std::min(h_, int(std::ceil(r.y + r.h)));
    float cx = r.x + r.w * 0.5f, cy = r.y + r.h * 0.5f;
    float hw = r.w * 0.5f - rad, hh = r.h * 0.5f - rad;
    // Half-width of the row interior lying at least `inset` inside the shape.
    auto innerHalf = [&](float qy, float inset) {
        float r = rad - inset;
        if (qy > r) return -1.0f;
        return qy <= 0 ? hw + r : hw + std::sqrt(r * r - qy * qy);
    };
    for (int y = y0; y < y1; ++y) {
        float qy = std::fabs(y + 0.5f - cy) - hh;
        // Fills: pixels whose center is >= 1px inside are fully covered, so
        // the distance computation is skipped there (strokes keep the exact path).
        int fx0 = x1, fx1 = x1;
        float full = th > 0 ? -1.0f : innerHalf(qy, 1.0f);
        if (full > 0) {
            fx0 = std::max(x0, int(std::ceil(cx - full - 0.5f)));
            fx1 = std::min(x1, int(std::floor(cx + full - 0.5f)) + 1);
        }
        for (int x = x0; x < x1; ++x) {
            if (x == fx0 && fx1 > fx0) {
                span(fx0, fx1, y, c);
                x = fx1 - 1;
                continue;
            }
            float qx = std::fabs(x + 0.5f - cx) - hw;
            float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
            // signed distance to the rounded rectangle (negative inside)
            float d = ((ox > 0 || oy > 0) ? std::sqrt(ox * ox + oy * oy) : 0.0f) + std::min(std::max(qx, qy), 0.0f) - rad;
            float cov = std::clamp(0.5f - d, 0.0f, 1.0f);
            if (th > 0) cov -= std::clamp(0.5f - (d + th), 0.0f, 1.0f);
            if (cov > 0.0f) blend(x, y, c, cov);
        }
    }
}

void Canvas::drawMask(int x, int y, const uint8_t* mask, int mw, int mh, Color c) {
    int lo = 0, hi = w_;
    if (clip_.on) {
        lo = std::max(lo, int(std::floor(clip_.x0)));
        hi = std::min(hi, int(std::ceil(clip_.x1)));
    }
    for (int j = 0; j < mh; ++j) {
        int py = y + j;
        if (py < 0 || py >= h_) continue;
        for (int i = 0; i < mw; ++i) {
            int px = x + i;
            if (px < lo || px >= hi) continue;
            uint8_t m = mask[j * mw + i];
            if (!m) continue;
            float cov = m * (1.0f / 255.0f);
            if (clip_.on) {
                float cx = px + 0.5f;
                if (clip_.fadeL > 0) cov *= std::clamp((cx - clip_.x0) / clip_.fadeL, 0.0f, 1.0f);
                if (clip_.fadeR > 0) cov *= std::clamp((clip_.x1 - cx) / clip_.fadeR, 0.0f, 1.0f);
            }
            blend(px, py, c, cov);
        }
    }
}

namespace {
// Signed distance from (px, py) to a rounded rectangle (negative inside).
float roundRectDist(float px, float py, Rect r, float rad) {
    float qx = std::fabs(px - (r.x + r.w * 0.5f)) - (r.w * 0.5f - rad);
    float qy = std::fabs(py - (r.y + r.h * 0.5f)) - (r.h * 0.5f - rad);
    float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - rad;
}
} // namespace

void Canvas::drawImage(const Image& img, Rect dst, float radius, float opacity) {
    if (img.empty() || dst.w <= 0 || dst.h <= 0) return;
    radius = std::min(radius, std::min(dst.w, dst.h) * 0.5f);
    // Cover: crop the source to the destination's aspect ratio, centered.
    float sw = float(img.w), sh = float(img.h), sx0 = 0, sy0 = 0;
    if (sw / sh > dst.w / dst.h) { float nw = sh * dst.w / dst.h; sx0 = (sw - nw) * 0.5f; sw = nw; }
    else { float nh = sw * dst.h / dst.w; sy0 = (sh - nh) * 0.5f; sh = nh; }
    const float kx = sw / dst.w, ky = sh / dst.h;
    int x0 = std::max(0, int(std::floor(dst.x))), y0 = std::max(0, int(std::floor(dst.y)));
    int x1 = std::min(w_, int(std::ceil(dst.right()))), y1 = std::min(h_, int(std::ceil(dst.bottom())));
    const uint8_t* src = img.px.data();
    for (int y = y0; y < y1; ++y) {
        float fy = std::clamp(sy0 + (y + 0.5f - dst.y) * ky - 0.5f, 0.0f, float(img.h - 1));
        int iy = int(fy);
        int iy1 = std::min(iy + 1, img.h - 1);
        float ty = fy - iy;
        for (int x = x0; x < x1; ++x) {
            float cov = std::clamp(0.5f - roundRectDist(x + 0.5f, y + 0.5f, dst, radius), 0.0f, 1.0f);
            if (cov <= 0) continue;
            float fx = std::clamp(sx0 + (x + 0.5f - dst.x) * kx - 0.5f, 0.0f, float(img.w - 1));
            int ix = int(fx);
            int ix1 = std::min(ix + 1, img.w - 1);
            float tx = fx - ix;
            const uint8_t* p00 = src + (size_t(iy) * img.w + ix) * 4;
            const uint8_t* p10 = src + (size_t(iy) * img.w + ix1) * 4;
            const uint8_t* p01 = src + (size_t(iy1) * img.w + ix) * 4;
            const uint8_t* p11 = src + (size_t(iy1) * img.w + ix1) * 4;
            // Alpha-weighted bilinear, so transparent texels don't darken edges.
            float w00 = (1 - tx) * (1 - ty) * p00[3], w10 = tx * (1 - ty) * p10[3];
            float w01 = (1 - tx) * ty * p01[3], w11 = tx * ty * p11[3];
            float a = w00 + w10 + w01 + w11;
            if (a <= 0.5f) continue;
            float inv = 1.0f / a;
            Color c{uint8_t((p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11) * inv + 0.5f),
                    uint8_t((p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11) * inv + 0.5f),
                    uint8_t((p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11) * inv + 0.5f),
                    uint8_t(std::min(255.0f, a + 0.5f))};
            blend(x, y, c, cov * opacity);
        }
    }
}

void Canvas::drawShadow(Rect r, float radius, float blur, Color c) {
    if (blur <= 0) return;
    int x0 = std::max(0, int(std::floor(r.x - blur))), y0 = std::max(0, int(std::floor(r.y - blur)));
    int x1 = std::min(w_, int(std::ceil(r.right() + blur))), y1 = std::min(h_, int(std::ceil(r.bottom() + blur)));
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            float d = roundRectDist(x + 0.5f, y + 0.5f, r, radius);
            if (d < -blur) continue; // well inside: covered by the panel anyway
            // Gaussian-like falloff from the edge outwards.
            float t = std::clamp((d + blur * 0.25f) / blur, 0.0f, 1.0f);
            float f = (1 - t) * (1 - t);
            f = f * f;
            if (f > 0.002f) blend(x, y, c, f);
        }
}
