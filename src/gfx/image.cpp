#include "gfx/image.h"

#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

template <class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T** operator&() { return &p; }
    T* operator->() const { return p; }
    operator T*() const { return p; }
};

IWICImagingFactory* factory() {
    static IWICImagingFactory* f = [] {
        IWICImagingFactory* r = nullptr;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&r));
        return r;
    }();
    return f;
}

} // namespace

bool loadImage(const std::wstring& path, int maxW, int maxH, Image& out) {
    IWICImagingFactory* f = factory();
    if (!f) return false;
    Com<IWICBitmapDecoder> dec;
    if (FAILED(f->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &dec))) return false;
    Com<IWICBitmapFrameDecode> frame;
    if (FAILED(dec->GetFrame(0, &frame))) return false;
    UINT w = 0, h = 0;
    frame->GetSize(&w, &h);
    if (!w || !h) return false;
    float s = std::min({1.0f, float(maxW) / float(w), float(maxH) / float(h)});
    UINT tw = std::max(1u, UINT(std::lround(w * s))), th = std::max(1u, UINT(std::lround(h * s)));

    IWICBitmapSource* src = frame;
    Com<IWICBitmapScaler> scaler;
    if (tw != w || th != h) {
        // Fant: area averaging, sharp and alias-free when shrinking a lot.
        if (FAILED(f->CreateBitmapScaler(&scaler)) || FAILED(scaler->Initialize(frame, tw, th, WICBitmapInterpolationModeFant)))
            return false;
        src = scaler;
    }
    Com<IWICFormatConverter> conv;
    if (FAILED(f->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(src, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom)))
        return false;
    out.w = int(tw);
    out.h = int(th);
    out.px.resize(size_t(tw) * th * 4);
    return SUCCEEDED(conv->CopyPixels(nullptr, tw * 4, UINT(out.px.size()), out.px.data()));
}

bool imageFromBitmap(HBITMAP bmp, Image& out) {
    BITMAP bm{};
    if (!GetObjectW(bmp, sizeof(bm), &bm) || bm.bmBitsPixel != 32 || !bm.bmBits) return false;
    out.w = bm.bmWidth;
    out.h = std::abs(bm.bmHeight);
    out.px.resize(size_t(out.w) * out.h * 4);
    bool hasAlpha = false;
    for (int y = 0; y < out.h; ++y) {
        // DIB sections are bottom-up unless the height is negative.
        int sy = bm.bmHeight > 0 ? out.h - 1 - y : y;
        const uint8_t* s = static_cast<const uint8_t*>(bm.bmBits) + size_t(sy) * bm.bmWidthBytes;
        uint8_t* d = &out.px[size_t(y) * out.w * 4];
        for (int x = 0; x < out.w; ++x, s += 4, d += 4) {
            // Premultiplied BGRA -> straight RGBA.
            uint8_t a = s[3];
            hasAlpha |= a != 0;
            if (a == 0) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 0; continue; }
            d[0] = uint8_t(std::min(255, s[2] * 255 / a));
            d[1] = uint8_t(std::min(255, s[1] * 255 / a));
            d[2] = uint8_t(std::min(255, s[0] * 255 / a));
            d[3] = a;
        }
    }
    if (!hasAlpha) // no alpha channel at all: opaque
        for (size_t i = 3; i < out.px.size(); i += 4) out.px[i] = 255;
    return true;
}

void downscale(Image& img, int maxW, int maxH) {
    if (img.empty() || (img.w <= maxW && img.h <= maxH)) return;
    float s = std::min(float(maxW) / float(img.w), float(maxH) / float(img.h));
    int tw = std::max(1, int(std::lround(img.w * s))), th = std::max(1, int(std::lround(img.h * s)));
    Image out;
    out.w = tw;
    out.h = th;
    out.px.resize(size_t(tw) * th * 4);
    for (int y = 0; y < th; ++y) {
        int sy0 = y * img.h / th, sy1 = std::max(sy0 + 1, (y + 1) * img.h / th);
        for (int x = 0; x < tw; ++x) {
            int sx0 = x * img.w / tw, sx1 = std::max(sx0 + 1, (x + 1) * img.w / tw);
            // Alpha-weighted average of the source box.
            float r = 0, g = 0, b = 0, a = 0;
            for (int yy = sy0; yy < sy1; ++yy)
                for (int xx = sx0; xx < sx1; ++xx) {
                    const uint8_t* p = &img.px[(size_t(yy) * img.w + xx) * 4];
                    float pa = p[3];
                    r += p[0] * pa; g += p[1] * pa; b += p[2] * pa; a += pa;
                }
            uint8_t* d = &out.px[(size_t(y) * tw + x) * 4];
            int n = (sx1 - sx0) * (sy1 - sy0);
            if (a > 0) { d[0] = uint8_t(r / a + 0.5f); d[1] = uint8_t(g / a + 0.5f); d[2] = uint8_t(b / a + 0.5f); }
            d[3] = uint8_t(a / n + 0.5f);
        }
    }
    img = std::move(out);
}
