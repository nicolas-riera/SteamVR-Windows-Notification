#pragma once
// Colors of the VR scene around a notification, read from SteamVR's mirror
// (left eye) texture: the backdrop its Mica background is made from, like
// Windows makes Mica from what lies behind a window.
#include <dxgi.h>

#include <vector>

#include "core/mathx.h"

struct ID3D11ShaderResourceView;
struct ID3D11Texture2D;

// A coarse grid of colors (heavily blurred backdrop).
struct MicaField {
    int cols = 0, rows = 0;
    std::vector<float> rgb;            // cols * rows * 3, sRGB in [0, 1]
    float px0 = 0, py0 = 0, px1 = 0, py1 = 0; // the panel's rectangle in grid units
    bool valid() const { return cols > 0 && rows > 0 && rgb.size() == size_t(cols) * rows * 3; }
};

class MirrorSampler {
public:
    ~MirrorSampler();
    // Frees the mirror texture view and staging texture (before the device).
    void release();
    // `corners`: the panel's corners in the standing universe (top-left,
    // top-right, bottom-right, bottom-left); `head`: current head pose.
    bool sample(const Vec3 corners[4], const Mat34& head, MicaField& out);

private:
    bool open();
    ID3D11ShaderResourceView* srv_ = nullptr;
    ID3D11Texture2D* mirror_ = nullptr;
    ID3D11Texture2D* staging_ = nullptr;
    int texW_ = 0, texH_ = 0, stagingW_ = 0, stagingH_ = 0;
    DXGI_FORMAT format_ = DXGI_FORMAT_UNKNOWN;
    float left_ = -1, right_ = 1, top_ = -1, bottom_ = 1; // eye projection tangents
    Mat34 eyeToHead_ = Mat34::identity();
    double retryAt_ = 0;
};
