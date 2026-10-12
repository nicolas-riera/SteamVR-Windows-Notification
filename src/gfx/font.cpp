#include "gfx/font.h"
#include "gfx/textshape.h"

#include <windows.h>

#include <cmath>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include "core/log.h"

struct FontSet::Face {
    HANDLE file = INVALID_HANDLE_VALUE;
    HANDLE mapping = nullptr;
    const unsigned char* data = nullptr;
    stbtt_fontinfo info{};
    float emScale = 0; // scale for a 1px em

    ~Face() {
        if (data) UnmapViewOfFile(data);
        if (mapping) CloseHandle(mapping);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
};

uint32_t nextCodepoint(std::string_view s, size_t& i) {
    unsigned char c = (unsigned char)s[i++];
    if (c < 0x80) return c;
    int extra = (c >= 0xF0) ? 3 : (c >= 0xE0) ? 2 : (c >= 0xC0) ? 1 : 0;
    uint32_t cp = c & (0x3F >> extra);
    for (int k = 0; k < extra && i < s.size(); ++k) cp = (cp << 6) | ((unsigned char)s[i++] & 0x3F);
    return cp;
}

FontSet::FontSet() = default;
FontSet::~FontSet() = default;

bool FontSet::addFace(const wchar_t* name) {
    wchar_t dir[MAX_PATH];
    GetWindowsDirectoryW(dir, MAX_PATH);
    std::wstring path = std::wstring(dir) + L"\\Fonts\\" + name;
    auto f = std::make_unique<Face>();
    f->file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f->file == INVALID_HANDLE_VALUE) return false;
    f->mapping = CreateFileMappingW(f->file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (!f->mapping) return false;
    f->data = (const unsigned char*)MapViewOfFile(f->mapping, FILE_MAP_READ, 0, 0, 0);
    if (!f->data) return false;
    int offset = stbtt_GetFontOffsetForIndex(f->data, 0);
    if (offset < 0 || !stbtt_InitFont(&f->info, f->data, offset)) return false;
    f->emScale = stbtt_ScaleForMappingEmToPixels(&f->info, 1.0f);
    faces_.push_back(std::move(f));
    return true;
}

bool FontSet::init() {
    if (!addFace(L"segoeui.ttf")) { LOG("font: segoeui.ttf missing"); return false; }
    if (!addFace(L"seguisb.ttf")) addFace(L"segoeui.ttf");
    // Fallbacks for the other scripts of the translations (only mapped, never copied).
    for (const wchar_t* fb : {L"seguisym.ttf", L"msyh.ttc", L"msjh.ttc", L"malgun.ttf", L"YuGothR.ttc", L"LeelawUI.ttf",
                              L"Nirmala.ttc", L"Nirmala.ttf", L"seguihis.ttf", L"seguiemj.ttf"})
        addFace(fb);
    if (addFace(L"SegoeIcons.ttf") || addFace(L"segmdl2.ttf")) iconFace_ = int(faces_.size()) - 1;

    int x0, y0, x1, y1;
    const Face& f = *faces_[0];
    if (stbtt_GetCodepointBox(&f.info, 'H', &x0, &y0, &x1, &y1)) capHeight_ = y1 * f.emScale;
    return true;
}

int FontSet::findGlyph(uint32_t cp, FontStyle st, int& face) const {
    int first = st == FontStyle::Bold ? 1 : 0;
    int g = stbtt_FindGlyphIndex(&faces_[first]->info, int(cp));
    if (g) { face = first; return g; }
    for (int i = 2; i < int(faces_.size()); ++i) {
        if (i == iconFace_) continue;
        g = stbtt_FindGlyphIndex(&faces_[i]->info, int(cp));
        if (g) { face = i; return g; }
    }
    face = first;
    return 0; // .notdef
}

const FontSet::Glyph& FontSet::glyph(int face, int index, float px) {
    uint64_t key = (uint64_t(face) << 48) | (uint64_t(index) << 16) | uint64_t(px * 4.0f);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;
    if (cache_.size() > 3000) cache_.clear(); // bounded memory

    const Face& f = *faces_[face];
    float scale = f.emScale * px;
    Glyph g;
    int adv, lsb;
    stbtt_GetGlyphHMetrics(&f.info, index, &adv, &lsb);
    g.advance = adv * scale;
    int x0, y0, x1, y1;
    stbtt_GetGlyphBitmapBox(&f.info, index, scale, scale, &x0, &y0, &x1, &y1);
    g.w = x1 - x0;
    g.h = y1 - y0;
    g.x0 = x0;
    g.y0 = y0;
    if (g.w > 0 && g.h > 0) {
        g.bmp.resize(size_t(g.w) * size_t(g.h));
        stbtt_MakeGlyphBitmap(&f.info, g.bmp.data(), g.w, g.h, g.w, scale, scale, index);
    }
    return cache_.emplace(key, std::move(g)).first->second;
}

float FontSet::measure(std::string_view text, float px, FontStyle st) {
    std::string shaped;
    std::string_view s = shapeForDisplay(text, shaped);
    float w = 0;
    for (size_t i = 0; i < s.size();) {
        int face;
        int gi = findGlyph(nextCodepoint(s, i), st, face);
        w += glyph(face, gi, px).advance;
    }
    return w;
}

float FontSet::draw(Canvas& c, float x, float baseline, std::string_view text, float px, Color col, FontStyle st) {
    std::string shaped;
    std::string_view s = shapeForDisplay(text, shaped);
    float pen = x;
    int by = int(std::lround(baseline));
    for (size_t i = 0; i < s.size();) {
        int face;
        int gi = findGlyph(nextCodepoint(s, i), st, face);
        const Glyph& g = glyph(face, gi, px);
        if (!g.bmp.empty()) c.drawMask(int(std::lround(pen)) + g.x0, by + g.y0, g.bmp.data(), g.w, g.h, col);
        pen += g.advance;
    }
    return pen - x;
}

std::string FontSet::ellipsize(std::string_view s, float px, float maxW, FontStyle st) {
    if (measure(s, px, st) <= maxW) return std::string(s);
    const char* dots = "\xE2\x80\xA6"; // …
    float budget = maxW - measure(dots, px, st);
    float w = 0;
    size_t cut = 0;
    for (size_t i = 0; i < s.size();) {
        size_t start = i;
        int face;
        int gi = findGlyph(nextCodepoint(s, i), st, face);
        w += glyph(face, gi, px).advance;
        if (w > budget) break;
        cut = i;
        (void)start;
    }
    return std::string(s.substr(0, cut)) + dots;
}

float FontSet::capCenterOffset(float px) const { return capHeight_ * px * 0.5f; }

bool FontSet::drawIcon(Canvas& c, float cx, float cy, uint32_t cp, float px, Color col) {
    if (iconFace_ < 0) return false;
    int gi = stbtt_FindGlyphIndex(&faces_[iconFace_]->info, int(cp));
    if (!gi) return false;
    const Glyph& g = glyph(iconFace_, gi, px);
    if (!g.bmp.empty())
        c.drawMask(int(std::lround(cx - g.w * 0.5f)), int(std::lround(cy - g.h * 0.5f)), g.bmp.data(), g.w, g.h, col);
    return true;
}
