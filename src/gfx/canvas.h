#pragma once
// Tiny CPU rasterizer: RGBA8 (straight alpha), anti-aliased rounded shapes.
#include <algorithm>
#include <cstdint>
#include <vector>

struct Image;

struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 255;
};

constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return Color{r, g, b, a}; }

inline Color withAlpha(Color c, uint8_t a) { c.a = a; return c; }
inline Color lighten(Color c, int d) {
    auto f = [d](uint8_t v) { return uint8_t(std::clamp(int(v) + d, 0, 255)); };
    return Color{f(c.r), f(c.g), f(c.b), c.a};
}
inline Color mix(Color a, Color b, float t) {
    auto f = [t](uint8_t x, uint8_t y) { return uint8_t(x + (y - x) * t + 0.5f); };
    return Color{f(a.r, b.r), f(a.g, b.g), f(a.b, b.b), f(a.a, b.a)};
}

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect insetX(float d) const { return {x + d, y, w - 2 * d, h}; }
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    float cx() const { return x + w * 0.5f; }
    float cy() const { return y + h * 0.5f; }
    // Split horizontally: left part of width `lw` (negative = from the right).
    Rect left(float lw) const { return {x, y, lw, h}; }
    Rect rightPart(float rw) const { return {x + w - rw, y, rw, h}; }
};

class Canvas {
public:
    void resize(int w, int h);
    void clear(Color c = Color{0, 0, 0, 0});
    int width() const { return w_; }
    int height() const { return h_; }
    const uint8_t* data() const { return px_.data(); }

    void fillRect(Rect r, Color c);
    void fillRoundRect(Rect r, float radius, Color c) { shape(r, radius, 0, c); }
    void strokeRoundRect(Rect r, float radius, float thickness, Color c) { shape(r, radius, thickness, c); }
    void fillCircle(float cx, float cy, float rad, Color c) { shape({cx - rad, cy - rad, rad * 2, rad * 2}, rad, 0, c); }
    void strokeCircle(float cx, float cy, float rad, float th, Color c) { shape({cx - rad, cy - rad, rad * 2, rad * 2}, rad, th, c); }
    // 8-bit coverage mask (glyphs), tinted with c.
    void drawMask(int x, int y, const uint8_t* mask, int mw, int mh, Color c);
    // Horizontal clip for masks (scrolling text), each edge optionally faded
    // over a few pixels.
    void setMaskClip(float x0, float x1, float fadeL, float fadeR) { clip_ = {x0, x1, fadeL, fadeR, true}; }
    void clearMaskClip() { clip_.on = false; }

    // Image scaled (bilinear) to cover `dst`, cropped to a rounded rectangle
    // (radius >= half the size: circle).
    void drawImage(const Image& img, Rect dst, float radius, float opacity = 1.0f);
    // Soft drop shadow around a rounded rectangle.
    void drawShadow(Rect r, float radius, float blur, Color c);
    // Makes a rounded rectangle transparent (anti-aliased edges).
    void eraseRoundRect(Rect r, float radius);

    void blend(int x, int y, Color c, float coverage);

private:
    void span(int x0, int x1, int y, Color c);
    void shape(Rect r, float radius, float thickness, Color c);
    int w_ = 0, h_ = 0;
    std::vector<uint8_t> px_;
    struct { float x0, x1, fadeL, fadeR; bool on; } clip_{0, 0, 0, 0, false};
};
