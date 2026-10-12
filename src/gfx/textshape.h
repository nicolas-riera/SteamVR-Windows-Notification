#pragma once
// Minimal complex-script support for the glyph-by-glyph renderer:
//  - Arabic: contextual forms (Presentation Forms-B) and lam-alef ligatures;
//  - right-to-left order for Arabic / Hebrew (simplified bidi: Latin words
//    and numbers inside stay left-to-right, brackets are mirrored);
//  - Devanagari: the short-i vowel sign is moved before its consonant cluster.
// Strings without such scripts are returned untouched (no allocation).
#include <string>
#include <string_view>

// Returns the visual-order string to draw. The view may point into `storage`.
std::string_view shapeForDisplay(std::string_view logical, std::string& storage);
