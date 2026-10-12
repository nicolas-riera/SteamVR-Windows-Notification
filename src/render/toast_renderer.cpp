#include "render/toast_renderer.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "gfx/font.h"
#include "render/mica.h"

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
        t.tint = rgb(243, 243, 243);
        t.tintOpacity = 0.5f;
        t.text = rgb(0, 0, 0, 228);
        t.text2 = rgb(0, 0, 0, 155);
        t.stroke = rgb(0, 0, 0, 26);
        t.control = rgb(255, 255, 255, 179);
        t.controlStroke = rgb(0, 0, 0, 20);
        t.accent = mix(accent, rgb(0, 0, 0), 0.2f);
        t.track = rgb(0, 0, 0, 60);
    } else {
        t.tint = rgb(44, 44, 44);
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

void ToastRenderer::render(const ToastVisual& v, const Theme& th, float s, bool mica, Canvas& out) {
    const float pad = kShadowPad * s;
    const float h = layout(v, nullptr, th, s, 0, 0);
    const int cw = int(std::ceil(kWidth * s + 2 * pad)), ch = int(std::ceil(h * s + 2 * pad));
    out.resize(cw, ch);
    out.clear();
    Rect panel{pad, pad, kWidth * s, h * s};
    const float radius = kRadius * s;

    // Soft shadow, slightly lower than the panel (light from above).
    Rect shadow = panel;
    shadow.y += 4 * s;
    out.drawShadow(shadow, radius, 16 * s, rgb(0, 0, 0, th.light ? 60 : 110));

    mica_.render(int(panel.w), int(panel.h), th.tint, th.tintOpacity, mica, backdrop_);
    out.drawImage(backdrop_, panel, radius);
    out.strokeRoundRect(panel, radius, std::max(1.0f, s), th.stroke);
    layout(v, &out, th, s, panel.x, panel.y);
}
