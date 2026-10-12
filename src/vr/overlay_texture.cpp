#include "vr/overlay_texture.h"

#include <d3d11.h>
#include <dxgi.h>

#include <cstring>

#include "core/log.h"
#include "gfx/canvas.h"

namespace {

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
bool g_tried = false;

// One small device shared by every overlay, on the adapter driving the HMD.
bool device() {
    if (g_tried) return g_device != nullptr;
    g_tried = true;
    int32_t adapterIndex = -1;
    vr::VRSystem()->GetDXGIOutputInfo(&adapterIndex);
    IDXGIFactory1* factory = nullptr;
    IDXGIAdapter1* adapter = nullptr;
    if (adapterIndex >= 0 && SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory)))
        factory->EnumAdapters1(UINT(adapterIndex), &adapter);
    HRESULT hr = D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_SINGLETHREADED, nullptr, 0, D3D11_SDK_VERSION, &g_device, nullptr,
                                   &g_context);
    if (adapter) adapter->Release();
    if (factory) factory->Release();
    if (FAILED(hr)) {
        LOG("d3d11: device creation failed (0x%08lx), using SetOverlayRaw", hr);
        g_device = nullptr;
        return false;
    }
    return true;
}

void releaseTex(ID3D11Texture2D*& t) {
    if (t) { t->Release(); t = nullptr; }
}

} // namespace

OverlayTexture::~OverlayTexture() {
    release();
    releaseTex(retired_);
}

void OverlayTexture::release() {
    releaseTex(tex_);
    releaseTex(staging_);
    w_ = h_ = 0;
}

bool OverlayTexture::ensure(int w, int h) {
    if (w == w_ && h == h_ && tex_) return true;
    // Size changed: the current texture may still be on screen, keep it alive
    // until the new one has been submitted.
    releaseTex(retired_);
    retired_ = tex_;
    tex_ = nullptr;
    releaseTex(staging_);
    retireAfter_ = 3; // freed after a few uploads with the new size
    w_ = h_ = 0;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = UINT(w);
    d.Height = UINT(h);
    d.MipLevels = 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(g_device->CreateTexture2D(&d, nullptr, &tex_))) { release(); return false; }
    d.Usage = D3D11_USAGE_STAGING;
    d.BindFlags = 0;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(g_device->CreateTexture2D(&d, nullptr, &staging_))) { release(); return false; }
    w_ = w;
    h_ = h;
    return true;
}

void OverlayTexture::upload(vr::VROverlayHandle_t overlay, const Canvas& c) {
    if (!device() || !ensure(c.width(), c.height())) {
        vr::VROverlay()->SetOverlayRaw(overlay, const_cast<uint8_t*>(c.data()), uint32_t(c.width()), uint32_t(c.height()), 4);
        return;
    }
    // Our own staging texture + a GPU copy: UpdateSubresource made the driver
    // keep large internal upload buffers (~2x less memory this way). One
    // texture is enough: commands run in order, so SteamVR's copy of it
    // (queued below) always reads this frame's content.
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(g_context->Map(staging_, 0, D3D11_MAP_WRITE, 0, &m))) return;
    const size_t row = size_t(c.width()) * 4;
    for (int y = 0; y < c.height(); ++y)
        std::memcpy(static_cast<uint8_t*>(m.pData) + size_t(y) * m.RowPitch, c.data() + size_t(y) * row, row);
    g_context->Unmap(staging_, 0);
    g_context->CopyResource(tex_, staging_);
    vr::Texture_t vt{tex_, vr::TextureType_DirectX, vr::ColorSpace_Auto};
    vr::EVROverlayError e = vr::VROverlay()->SetOverlayTexture(overlay, &vt);
    // SteamVR copies the texture through *our* device context: flush so that
    // copy actually runs now, not at our next upload (else: blank overlay).
    g_context->Flush();
    if (e != vr::VROverlayError_None) LOG("d3d11: SetOverlayTexture: %s", vr::VROverlay()->GetOverlayErrorNameFromEnum(e));
    if (retireAfter_ > 0 && --retireAfter_ == 0) releaseTex(retired_);
}

void OverlayTexture::releaseDevice() {
    if (g_context) { g_context->ClearState(); g_context->Flush(); g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
    g_tried = false;
}

ID3D11Device* OverlayTexture::sharedDevice() { return device() ? g_device : nullptr; }
ID3D11DeviceContext* OverlayTexture::sharedContext() { return device() ? g_context : nullptr; }
