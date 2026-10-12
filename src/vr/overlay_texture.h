#pragma once
#include <openvr.h>

struct ID3D11Texture2D;
class Canvas;

// Uploads a canvas to an overlay through a D3D11 texture. SetOverlayRaw
// recreates the texture asynchronously on every call, which makes the
// overlay blink on each refresh; a real texture updated in place does not.
// Falls back to SetOverlayRaw if D3D11 is unavailable.
class OverlayTexture {
public:
    OverlayTexture() = default;
    ~OverlayTexture();
    OverlayTexture(const OverlayTexture&) = delete;
    OverlayTexture& operator=(const OverlayTexture&) = delete;

    void upload(vr::VROverlayHandle_t overlay, const Canvas& canvas);

private:
    bool ensure(int w, int h);
    void release();

    ID3D11Texture2D* tex_ = nullptr;     // what SteamVR copies from
    ID3D11Texture2D* staging_ = nullptr; // CPU-writable, copied into tex_
    ID3D11Texture2D* retired_ = nullptr; // previous size, freed a bit later
    int retireAfter_ = 0;
    int w_ = 0, h_ = 0;
};
