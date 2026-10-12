#include "gfx/textshape.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "gfx/font.h"

namespace {

// Arabic letters U+0621..U+064A: first presentation form (isolated) and how
// many forms follow (2 = isolated/final: joins to the previous letter only,
// 4 = isolated/final/initial/medial: joins on both sides). 0 = no forms.
struct ArabicForm { uint16_t base; uint8_t count; };
constexpr ArabicForm kArabic[] = {
    {0xFE80, 1}, {0xFE81, 2}, {0xFE83, 2}, {0xFE85, 2}, {0xFE87, 2}, {0xFE89, 4}, {0xFE8D, 2}, {0xFE8F, 4}, // 0621-0628
    {0xFE93, 2}, {0xFE95, 4}, {0xFE99, 4}, {0xFE9D, 4}, {0xFEA1, 4}, {0xFEA5, 4}, {0xFEA9, 2}, {0xFEAB, 2}, // 0629-0630
    {0xFEAD, 2}, {0xFEAF, 2}, {0xFEB1, 4}, {0xFEB5, 4}, {0xFEB9, 4}, {0xFEBD, 4}, {0xFEC1, 4}, {0xFEC5, 4}, // 0631-0638
    {0xFEC9, 4}, {0xFECD, 4}, {0, 0},      {0, 0},      {0, 0},      {0, 0},      {0, 0},      {0, 0},      // 0639-0640
    {0xFED1, 4}, {0xFED5, 4}, {0xFED9, 4}, {0xFEDD, 4}, {0xFEE1, 4}, {0xFEE5, 4}, {0xFEE9, 4}, {0xFEED, 2}, // 0641-0648
    {0xFEEF, 2}, {0xFEF1, 4},                                                                              // 0649-064A
};

bool isArabicLetter(uint32_t c) { return c >= 0x0621 && c <= 0x064A; }
bool isMark(uint32_t c) {
    return (c >= 0x064B && c <= 0x065F) || c == 0x0670 || (c >= 0x0591 && c <= 0x05C7 && c != 0x05BE && c != 0x05C0 && c != 0x05C3 && c != 0x05C6) ||
           (c >= 0x0900 && c <= 0x0903) || (c >= 0x093A && c <= 0x094F && c != 0x093F) || (c >= 0x0951 && c <= 0x0957) ||
           (c >= 0x0962 && c <= 0x0963) || (c >= 0x0300 && c <= 0x036F);
}
// Joins to the next letter (dual-joining) / to the previous one (any joining).
bool joinsNext(uint32_t c) { return c == 0x0640 || (isArabicLetter(c) && kArabic[c - 0x0621].count == 4); }
bool joinsPrev(uint32_t c) { return c == 0x0640 || (isArabicLetter(c) && kArabic[c - 0x0621].count >= 2); }

bool isRtl(uint32_t c) { return (c >= 0x0590 && c <= 0x08FF) || (c >= 0xFB1D && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF); }
bool isNeutral(uint32_t c) {
    if (c < 0x80) return !((c >= '0' && c <= '9') || ((c | 0x20) >= 'a' && (c | 0x20) <= 'z'));
    return (c >= 0x00A0 && c <= 0x00BF) || (c >= 0x2000 && c <= 0x206F) || c == 0x00D7 || c == 0x00F7;
}
uint32_t mirror(uint32_t c) {
    switch (c) {
    case '(': return ')'; case ')': return '(';
    case '[': return ']'; case ']': return '[';
    case '{': return '}'; case '}': return '{';
    case '<': return '>'; case '>': return '<';
    case 0x00AB: return 0x00BB; case 0x00BB: return 0x00AB;
    default: return c;
    }
}

void shapeArabic(std::vector<uint32_t>& cp) {
    auto prevLetter = [&](size_t i) -> uint32_t {
        while (i-- > 0)
            if (!isMark(cp[i])) return cp[i];
        return 0;
    };
    auto nextIndex = [&](size_t i) -> size_t {
        for (++i; i < cp.size(); ++i)
            if (!isMark(cp[i])) return i;
        return cp.size();
    };
    std::vector<uint32_t> out;
    out.reserve(cp.size());
    for (size_t i = 0; i < cp.size(); ++i) {
        uint32_t c = cp[i];
        if (!isArabicLetter(c) || kArabic[c - 0x0621].count == 0) { out.push_back(c); continue; }
        bool prev = joinsNext(prevLetter(i));
        size_t n = nextIndex(i);
        uint32_t next = n < cp.size() ? cp[n] : 0;
        // Lam + alef: one ligature glyph.
        if (c == 0x0644 && (next == 0x0622 || next == 0x0623 || next == 0x0625 || next == 0x0627)) {
            uint32_t lig = next == 0x0622 ? 0xFEF5 : next == 0x0623 ? 0xFEF7 : next == 0x0625 ? 0xFEF9 : 0xFEFB;
            out.push_back(lig + (prev ? 1 : 0));
            for (size_t k = i + 1; k < n; ++k) out.push_back(cp[k]); // marks between them
            i = n;
            continue;
        }
        const ArabicForm& f = kArabic[c - 0x0621];
        bool nxt = f.count == 4 && joinsPrev(next);
        int form = 0; // isolated
        if (f.count >= 2 && prev && nxt) form = 3;
        else if (f.count >= 2 && prev) form = 1;
        else if (nxt) form = 2;
        out.push_back(uint32_t(f.base) + uint32_t(form));
    }
    cp.swap(out);
}

bool isDevaConsonant(uint32_t c) { return (c >= 0x0915 && c <= 0x0939) || (c >= 0x0958 && c <= 0x095F); }

// Short-i (U+093F) is written after its consonant cluster but drawn before it.
void reorderDevanagari(std::vector<uint32_t>& cp) {
    for (size_t i = 1; i < cp.size(); ++i) {
        if (cp[i] != 0x093F) continue;
        size_t start = i;
        // walk back over the cluster: C (nukta) (virama C (nukta))*
        size_t k = i;
        while (k > 0) {
            size_t j = k - 1;
            if (cp[j] == 0x093C) { k = j; continue; }            // nukta
            if (isDevaConsonant(cp[j])) {
                k = j;
                if (k >= 1 && cp[k - 1] == 0x094D) { k -= 1; continue; } // virama: the cluster goes on
                break;
            }
            break;
        }
        if (k < start && isDevaConsonant(cp[k])) {
            std::rotate(cp.begin() + std::ptrdiff_t(k), cp.begin() + std::ptrdiff_t(i), cp.begin() + std::ptrdiff_t(i) + 1);
        }
    }
}

// Simplified bidi for one line of UI text.
void reorderBidi(std::vector<uint32_t>& cp) {
    const size_t n = cp.size();
    std::vector<int8_t> dir(n); // 1 = RTL, 0 = LTR, -1 = neutral (resolved below)
    int base = -1;
    for (size_t i = 0; i < n; ++i) {
        uint32_t c = cp[i];
        dir[i] = isMark(c) ? -2 : isRtl(c) ? 1 : isNeutral(c) ? -1 : 0;
        if (base < 0 && dir[i] >= 0) base = dir[i];
    }
    if (base < 0) return;
    // Marks take their base's direction; neutrals the direction around them.
    for (size_t i = 0; i < n; ++i)
        if (dir[i] == -2) dir[i] = i > 0 ? dir[i - 1] : int8_t(base);
    for (size_t i = 0; i < n;) {
        if (dir[i] != -1) { ++i; continue; }
        size_t j = i;
        while (j < n && dir[j] == -1) ++j;
        int before = i > 0 ? dir[i - 1] : base, after = j < n ? dir[j] : base;
        int8_t d = int8_t(before == after ? before : base);
        for (size_t k = i; k < j; ++k) dir[k] = d;
        i = j;
    }
    // Runs of one direction.
    struct Run { size_t a, b; int d; };
    std::vector<Run> runs;
    for (size_t i = 0; i < n;) {
        size_t j = i;
        while (j < n && dir[j] == dir[i]) ++j;
        runs.push_back({i, j, dir[i]});
        i = j;
    }
    if (base == 1) std::reverse(runs.begin(), runs.end());
    std::vector<uint32_t> out;
    out.reserve(n);
    for (const Run& r : runs) {
        if (r.d == 0) {
            out.insert(out.end(), cp.begin() + std::ptrdiff_t(r.a), cp.begin() + std::ptrdiff_t(r.b));
            continue;
        }
        // Reverse by cluster (a letter keeps its marks right after it).
        size_t end = r.b;
        while (end > r.a) {
            size_t start = end - 1;
            while (start > r.a && isMark(cp[start])) --start;
            out.push_back(mirror(cp[start]));
            for (size_t k = start + 1; k < end; ++k) out.push_back(cp[k]);
            end = start;
        }
    }
    cp.swap(out);
}

void appendUtf8(std::string& s, uint32_t c) {
    if (c < 0x80) s += char(c);
    else if (c < 0x800) { s += char(0xC0 | (c >> 6)); s += char(0x80 | (c & 0x3F)); }
    else if (c < 0x10000) { s += char(0xE0 | (c >> 12)); s += char(0x80 | ((c >> 6) & 0x3F)); s += char(0x80 | (c & 0x3F)); }
    else {
        s += char(0xF0 | (c >> 18)); s += char(0x80 | ((c >> 12) & 0x3F));
        s += char(0x80 | ((c >> 6) & 0x3F)); s += char(0x80 | (c & 0x3F));
    }
}

} // namespace

std::string_view shapeForDisplay(std::string_view s, std::string& storage) {
    // Fast path: Hebrew / Arabic are U+0590..U+06FF (lead bytes D6..DB),
    // Devanagari U+0900..U+097F (E0 A4 / E0 A5).
    bool rtl = false, deva = false;
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char b = (unsigned char)s[i];
        if (b >= 0xD6 && b <= 0xDB) rtl = true;
        else if (b == 0xEF && i + 1 < s.size() && (unsigned char)s[i + 1] >= 0xAC && (unsigned char)s[i + 1] <= 0xBB) rtl = true; // U+FB00..U+FEFF
        else if (b == 0xE0 && i + 1 < s.size() && ((unsigned char)s[i + 1] == 0xA4 || (unsigned char)s[i + 1] == 0xA5)) deva = true;
    }
    if (!rtl && !deva) return s;
    std::vector<uint32_t> cp;
    cp.reserve(s.size());
    for (size_t i = 0; i < s.size();) cp.push_back(nextCodepoint(s, i));
    if (deva) reorderDevanagari(cp);
    if (rtl) {
        shapeArabic(cp);
        reorderBidi(cp);
    }
    storage.clear();
    for (uint32_t c : cp) appendUtf8(storage, c);
    return storage;
}
