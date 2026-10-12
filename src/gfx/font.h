#pragma once
// Text and icon rendering from the system fonts (Segoe UI + Segoe Fluent Icons),
// memory-mapped so nothing is copied into our process.
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gfx/canvas.h"

enum class FontStyle { Regular, Bold };

uint32_t nextCodepoint(std::string_view s, size_t& i); // UTF-8 decoding

class FontSet {
public:
    FontSet();
    ~FontSet();
    bool init();

    float measure(std::string_view s, float px, FontStyle st = FontStyle::Regular);
    // Draws with the baseline at `baseline`; returns the advance.
    float draw(Canvas& c, float x, float baseline, std::string_view s, float px, Color col, FontStyle st = FontStyle::Regular);
    std::string ellipsize(std::string_view s, float px, float maxW, FontStyle st = FontStyle::Regular);
    // Baseline offset that vertically centers capital letters in a box.
    float capCenterOffset(float px) const;
    // Icon glyph centered on (cx, cy). Returns false if the glyph is missing.
    bool drawIcon(Canvas& c, float cx, float cy, uint32_t cp, float px, Color col);

private:
    struct Face;
    struct Glyph {
        int w = 0, h = 0, x0 = 0, y0 = 0;
        float advance = 0;
        std::vector<uint8_t> bmp;
    };
    bool addFace(const wchar_t* file);
    int findGlyph(uint32_t cp, FontStyle st, int& face) const;
    const Glyph& glyph(int face, int index, float px);

    std::vector<std::unique_ptr<Face>> faces_; // [regular, bold, fallbacks...]
    int iconFace_ = -1;
    float capHeight_ = 0.7f;
    std::unordered_map<uint64_t, Glyph> cache_;
};
