#include "render/toast_renderer.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "gfx/font.h"

namespace {

// Windows 11 banner metrics (layout units).
constexpr float kPad = 16;          // side and bottom padding
constexpr float kHeaderH = 44;      // app icon + name row
constexpr float kRadius = 8;        // panel corners
constexpr float kLogo = 48;         // app logo override ("photo")
constexpr float kLogoGap = 12;
constexpr float kTitlePx = 14, kBodyPx = 14, kSmallPx = 12;
constexpr float kLine = 20, kSmallLine = 16;
constexpr int kTitleLines = 2, kBodyLines = 4;
constexpr float kControlH = 32;     // buttons and inputs
constexpr float kControlGap = 8;
constexpr float kImageRadius = 4;

Color rgba(uint32_t rgb, uint8_t a = 255) { return Color{uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb), a}; }

bool isSpace(char c) { return c == ' ' || c == '\t'; }

constexpr float kBayer[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

float lum(float r, float g, float b) { return 0.30f * r + 0.59f * g + 0.11f * b; }

// W3C "luminosity" blend: the backdrop's hue and saturation with luminosity `l`.
void setLum(float& r, float& g, float& b, float l) {
    float d = l - lum(r, g, b);
    r += d; g += d; b += d;
    float L = lum(r, g, b), n = std::min({r, g, b}), x = std::max({r, g, b});
    if (n < 0) { float k = L / std::max(L - n, 1e-4f); r = L + (r - L) * k; g = L + (g - L) * k; b = L + (b - L) * k; }
    if (x > 1) { float k = (1 - L) / std::max(x - L, 1e-4f); r = L + (r - L) * k; g = L + (g - L) * k; b = L + (b - L) * k; }
}

// Greedy word wrap; words wider than the line are split between characters
// (scripts without spaces). The last allowed line is ellipsized.
std::vector<std::string> wrap(FontSet& f, const std::string& text, float px, FontStyle st, float maxW, int maxLines) {
    std::vector<std::string> lines;
    size_t i = 0;
    while (i < text.size() && int(lines.size()) < maxLines) {
        size_t nl = text.find('\n', i);
        std::string para = text.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
        i = nl == std::string::npos ? text.size() : nl + 1;
        if (!para.empty() && para.back() == '\r') para.pop_back();
        std::string line;
        size_t p = 0;
        bool overflow = false;
        while (p < para.size()) {
            size_t e = p;
            while (e < para.size() && !isSpace(para[e])) ++e;
            while (e < para.size() && isSpace(para[e])) ++e;
            std::string word = para.substr(p, e - p);
            std::string cand = line + word;
            size_t trimEnd = cand.find_last_not_of(" \t");
            if (f.measure(cand.substr(0, trimEnd + 1), px, st) <= maxW) {
                line = cand;
                p = e;
                continue;
            }
            if (line.empty()) {
                // A single word longer than the line: cut it at a character.
                size_t q = p, fit = p;
                while (q < e) {
                    size_t n = q;
                    nextCodepoint(para, n);
                    if (f.measure(para.substr(p, n - p), px, st) > maxW && fit > p) break;
                    fit = q = n;
                }
                line = para.substr(p, fit - p);
                p = fit;
            }
            lines.push_back(line);
            line.clear();
            if (int(lines.size()) == maxLines) { overflow = true; break; }
        }
        if (overflow) {
            std::string& last = lines.back();
            last = f.ellipsize(last + " " + para.substr(p), px, maxW, st);
            return lines;
        }
        if (!line.empty() || para.empty()) lines.push_back(line);
    }
    if (i < text.size() && !lines.empty()) lines.back() = f.ellipsize(lines.back() + " \xE2\x80\xA6", px, maxW, st);
    for (auto& l : lines) {
        size_t e = l.find_last_not_of(" \t");
        l = e == std::string::npos ? std::string() : l.substr(0, e + 1);
    }
    return lines;
}

} // namespace

Theme Theme::make(bool light, uint32_t accentRgb) {
    Theme t;
    t.light = light;
    Color accent = rgba(accentRgb);
    if (light) {
        t.tint = rgb(243, 243, 243); // Mica light
        t.tintOpacity = 0.5f;
        t.text = rgb(0, 0, 0, 228);
        t.text2 = rgb(0, 0, 0, 155);
        t.stroke = rgb(0, 0, 0, 26);
        t.control = rgb(255, 255, 255, 179);
        t.controlStroke = rgb(0, 0, 0, 20);
        t.accent = mix(accent, rgb(0, 0, 0), 0.2f);
        t.track = rgb(0, 0, 0, 60);
    } else {
        t.tint = rgb(40, 40, 40); // Mica dark (as Windows' notifications)
        t.tintOpacity = 0.8f;
        t.text = rgb(255, 255, 255);
        t.text2 = rgb(255, 255, 255, 200);
        t.stroke = rgb(255, 255, 255, 22);
        t.control = rgb(255, 255, 255, 16);
        t.controlStroke = rgb(255, 255, 255, 24);
        t.accent = mix(accent, rgb(255, 255, 255), 0.45f); // Windows uses the lighter accent on dark
        t.track = rgb(255, 255, 255, 60);
    }
    t.accent.a = 255;
    return t;
}

// Lays out the panel content (origin = panel top-left, in canvas pixels);
// draws when `c` is set. Returns the panel height in layout units.
float ToastRenderer::layout(const ToastVisual& v, Canvas* c, const Theme& th, float s, float ox, float oy) {
    const Toast& t = v.toast;
    FontSet& f = fonts_;
    const float W = kWidth;
    auto X = [&](float x) { return ox + x * s; };
    auto Y = [&](float y) { return oy + y * s; };
    auto text = [&](float x, float lineTop, float lineH, const std::string& str, float px, Color col, FontStyle st) {
        if (c) f.draw(*c, X(x), Y(lineTop + lineH * 0.5f) + f.capCenterOffset(px * s), str, px * s, col, st);
    };

    // Header: app icon, app name, then "..." and close glyphs on the right.
    if (c) {
        if (!v.appIcon.empty()) c->drawImage(v.appIcon, {X(kPad), Y(14), 16 * s, 16 * s}, 0);
        float nameX = v.appIcon.empty() ? kPad : kPad + 16 + 8;
        std::string name = f.ellipsize(v.appName, kSmallPx * s, (W - nameX - 80) * s);
        text(nameX, 12, 20, name, kSmallPx, th.text2, FontStyle::Regular);
        f.drawIcon(*c, X(W - 28), Y(22), 0xE8BB, 10 * s, th.text2); // close
        f.drawIcon(*c, X(W - 60), Y(22), 0xE712, 14 * s, th.text2); // more
    }
    float y = kHeaderH;

    // Hero image: full width, at most 2:1.
    if (!v.hero.empty()) {
        float w = W - 2 * kPad;
        float h = w / std::clamp(v.hero.aspect(), 1.0f, 2.0f);
        if (c) c->drawImage(v.hero, {X(kPad), Y(y), w * s, h * s}, kImageRadius * s);
        y += h + 12;
    }

    // Logo + texts.
    float textX = kPad;
    float rowTop = y;
    if (!v.logo.empty()) {
        if (c) c->drawImage(v.logo, {X(kPad), Y(y), kLogo * s, kLogo * s}, (t.logo.circle ? kLogo * 0.5f : kImageRadius) * s);
        textX = kPad + kLogo + kLogoGap;
    }
    const float textW = (W - kPad - textX) * s;
    if (!t.title.empty()) {
        for (auto& line : wrap(f, t.title, kTitlePx * s, FontStyle::Bold, textW, kTitleLines)) {
            text(textX, y, kLine, line, kTitlePx, th.text, FontStyle::Bold);
            y += kLine;
        }
    }
    int bodyLeft = kBodyLines;
    for (const std::string& body : t.lines) {
        if (bodyLeft <= 0) break;
        auto lines = wrap(f, body, kBodyPx * s, FontStyle::Regular, textW, bodyLeft);
        bodyLeft -= int(lines.size());
        for (auto& line : lines) {
            text(textX, y, kLine, line, kBodyPx, th.text2, FontStyle::Regular);
            y += kLine;
        }
    }
    if (!t.attribution.empty()) {
        y += 2;
        text(textX, y, kSmallLine, f.ellipsize(t.attribution, kSmallPx * s, textW), kSmallPx, th.text2, FontStyle::Regular);
        y += kSmallLine;
    }
    for (const ToastProgress& p : t.progress) {
        y += 8;
        if (!p.title.empty()) {
            text(textX, y, kSmallLine, f.ellipsize(p.title, kSmallPx * s, textW), kSmallPx, th.text, FontStyle::Regular);
            y += kSmallLine + 4;
        }
        if (c) {
            Rect track{X(textX), Y(y), textW, 4 * s};
            c->fillRoundRect(track, 2 * s, th.track);
            if (p.value >= 0) {
                Rect fill = track;
                fill.w = std::max(track.h, float(track.w * p.value));
                c->fillRoundRect(fill, 2 * s, th.accent);
            } else {
                // Indeterminate: a static segment (no animation, so no redraws).
                c->fillRoundRect({track.x + track.w * 0.3f, track.y, track.w * 0.35f, track.h}, 2 * s, th.accent);
            }
        }
        y += 4 + 4;
        std::string right = p.valueString;
        if (right.empty() && p.value >= 0) right = std::to_string(int(std::lround(p.value * 100))) + " %";
        if (!p.status.empty() || !right.empty()) {
            float rw = right.empty() ? 0 : f.measure(right, kSmallPx * s);
            if (c && !right.empty())
                f.draw(*c, X(textX) + textW - rw, Y(y + kSmallLine * 0.5f) + f.capCenterOffset(kSmallPx * s), right, kSmallPx * s, th.text2);
            if (!p.status.empty())
                text(textX, y, kSmallLine, f.ellipsize(p.status, kSmallPx * s, textW - rw - 8 * s), kSmallPx, th.text2,
                     FontStyle::Regular);
            y += kSmallLine;
        }
    }
    if (!v.logo.empty()) y = std::max(y, rowTop + kLogo);

    // Inline image, full width, natural aspect (capped).
    if (!v.inlineImage.empty()) {
        y += 12;
        float w = W - 2 * kPad;
        float h = std::min(w / std::max(v.inlineImage.aspect(), 0.5f), 220.0f);
        if (c) c->drawImage(v.inlineImage, {X(kPad), Y(y), w * s, h * s}, kImageRadius * s);
        y += h;
    }

    // Inputs (shown with their placeholder), then buttons.
    for (const std::string& in : t.inputs) {
        y += kControlGap + (in == t.inputs.front() ? 4 : 0);
        if (c) {
            Rect r{X(kPad), Y(y), (W - 2 * kPad) * s, kControlH * s};
            c->fillRoundRect(r, 4 * s, th.control);
            c->strokeRoundRect(r, 4 * s, std::max(1.0f, s), th.controlStroke);
            // Bottom accent line of a text box.
            c->fillRect({r.x + 4 * s, r.bottom() - std::max(1.0f, s), r.w - 8 * s, std::max(1.0f, s)}, th.text2);
            text(kPad + 11, y, kControlH, f.ellipsize(in, kBodyPx * s, r.w - 22 * s), kBodyPx, th.text2, FontStyle::Regular);
        }
        y += kControlH;
    }
    if (!t.buttons.empty()) {
        y += 12;
        size_t n = std::min<size_t>(t.buttons.size(), 5);
        float bw = (W - 2 * kPad - kControlGap * float(n - 1)) / float(n);
        for (size_t i = 0; i < n && c; ++i) {
            Rect r{X(kPad + (bw + kControlGap) * float(i)), Y(y), bw * s, kControlH * s};
            c->fillRoundRect(r, 4 * s, th.control);
            c->strokeRoundRect(r, 4 * s, std::max(1.0f, s), th.controlStroke);
            std::string label = f.ellipsize(t.buttons[i], kBodyPx * s, r.w - 16 * s);
            float lw = f.measure(label, kBodyPx * s);
            f.draw(*c, r.cx() - lw * 0.5f, r.cy() + f.capCenterOffset(kBodyPx * s), label, kBodyPx * s, th.text);
        }
        y += kControlH;
    }
    return y + kPad;
}

float ToastRenderer::render(const ToastVisual& v, const Theme& th, float s, Canvas& out) {
    const float pad = kShadowPad * s;
    const float h = layout(v, nullptr, th, s, 0, 0);
    // Even sizes: the backdrop is exactly half as large.
    auto even = [](float x) { int n = int(std::ceil(x)); return n + (n & 1); };
    out.resize(even(kWidth * s + 2 * pad), even(h * s + 2 * pad));
    out.clear();
    Rect panel{pad, pad, kWidth * s, h * s};
    out.strokeRoundRect(panel, kRadius * s, std::max(1.0f, s), th.stroke);
    layout(v, &out, th, s, panel.x, panel.y);
    return h;
}

void ToastRenderer::renderBackdrop(const MicaField* field, const Theme& th, float panelH, float s, int w, int h, float fallbackOpacity,
                                   Canvas& out) {
    out.resize(w, h);
    out.clear();
    const float pad = kShadowPad * s;
    Rect panel{pad, pad, kWidth * s, panelH * s};
    const float radius = kRadius * s;

    // Soft shadow, slightly lower than the panel (light from above).
    Rect shadow = panel;
    shadow.y += 4 * s;
    out.drawShadow(shadow, radius, 16 * s, rgb(0, 0, 0, th.light ? 50 : 90));
    out.eraseRoundRect(panel, radius);

    if (!field || !field->valid()) {
        // No view of the scene: plain translucent tint.
        out.fillRoundRect(panel, radius, withAlpha(th.tint, uint8_t(std::lround(fallbackOpacity * 255))));
        return;
    }
    // Mica: the (very blurred) backdrop gets the tint's luminosity, then the
    // tint on top at its opacity. Like Windows, opaque: only colors show.
    Image mica;
    mica.w = int(std::ceil(panel.w));
    mica.h = int(std::ceil(panel.h));
    mica.px.resize(size_t(mica.w) * mica.h * 4);
    const float tr = th.tint.r / 255.0f, tg = th.tint.g / 255.0f, tb = th.tint.b / 255.0f, tl = lum(tr, tg, tb);
    const MicaField& f = *field;
    for (int y = 0; y < mica.h; ++y) {
        float gy = std::clamp(f.py0 + (y + 0.5f) / mica.h * (f.py1 - f.py0) - 0.5f, 0.0f, float(f.rows - 1));
        int iy = std::min(int(gy), f.rows - 1), iy1 = std::min(iy + 1, f.rows - 1);
        float ty = gy - iy;
        for (int x = 0; x < mica.w; ++x) {
            float gx = std::clamp(f.px0 + (x + 0.5f) / mica.w * (f.px1 - f.px0) - 0.5f, 0.0f, float(f.cols - 1));
            int ix = std::min(int(gx), f.cols - 1), ix1 = std::min(ix + 1, f.cols - 1);
            float tx = gx - ix;
            float c[3];
            for (int k = 0; k < 3; ++k) {
                auto at = [&](int cx, int cy) { return f.rgb[(size_t(cy) * f.cols + cx) * 3 + k]; };
                c[k] = (at(ix, iy) * (1 - tx) + at(ix1, iy) * tx) * (1 - ty) + (at(ix, iy1) * (1 - tx) + at(ix1, iy1) * tx) * ty;
            }
            setLum(c[0], c[1], c[2], tl);
            uint8_t* p = &mica.px[(size_t(y) * mica.w + x) * 4];
            // Ordered dither: the smooth gradients would band in 8 bits.
            const float dither = kBayer[(y & 3) * 4 + (x & 3)] / 16.0f;
            p[0] = uint8_t(std::min(255.0f, std::clamp(c[0] + (tr - c[0]) * th.tintOpacity, 0.0f, 1.0f) * 255.0f + dither));
            p[1] = uint8_t(std::min(255.0f, std::clamp(c[1] + (tg - c[1]) * th.tintOpacity, 0.0f, 1.0f) * 255.0f + dither));
            p[2] = uint8_t(std::min(255.0f, std::clamp(c[2] + (tb - c[2]) * th.tintOpacity, 0.0f, 1.0f) * 255.0f + dither));
            p[3] = 255;
        }
    }
    out.drawImage(mica, panel, radius);
}
