#pragma once
// RGBA8 (straight alpha) images decoded through WIC (PNG, JPEG, GIF, BMP,
// ICO, TIFF, WebP / HEIF when their codecs are installed).
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;
    bool empty() const { return w <= 0 || h <= 0; }
    float aspect() const { return h > 0 ? float(w) / float(h) : 1.0f; }
};

// Decodes the first frame, downscaled (never upscaled) to fit maxW x maxH.
// Needs COM initialized on the calling thread.
bool loadImage(const std::wstring& path, int maxW, int maxH, Image& out);
// Area-averaged downscale to fit maxW x maxH (no-op if already small enough).
void downscale(Image& img, int maxW, int maxH);
// Copies a 32-bit DIB section (e.g. from IShellItemImageFactory).
bool imageFromBitmap(HBITMAP bmp, Image& out);
