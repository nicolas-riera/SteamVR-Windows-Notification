#include "vr/mirror_sampler.h"

#include <d3d11.h>
#include <openvr.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/log.h"
#include "core/util.h"
#include "vr/overlay_texture.h"

namespace {

constexpr int kCols = 10, kRows = 6;  // grid over the sampled area
constexpr float kMargin = 0.35f;      // sampled area: the panel grown by this much on each side
constexpr int kMaxRegion = 1024;      // px, largest region read back
constexpr int kSamplesPerAxis = 96;   // pixels looked at along each axis

Vec3 transformPoint(const Mat34& m, const Vec3& p) {
    return {m.m[0][0] * p.x + m.m[0][1] * p.y + m.m[0][2] * p.z + m.m[0][3],
            m.m[1][0] * p.x + m.m[1][1] * p.y + m.m[1][2] * p.z + m.m[1][3],
            m.m[2][0] * p.x + m.m[2][1] * p.y + m.m[2][2] * p.z + m.m[2][3]};
}

float halfToFloat(uint16_t h) {
    uint32_t sign = (h >> 15) & 1, exp = (h >> 10) & 0x1F, mant = h & 0x3FF;
    if (exp == 0) return (sign ? -1.0f : 1.0f) * std::ldexp(float(mant), -24);
    if (exp == 31) return 1.0f;
    return (sign ? -1.0f : 1.0f) * std::ldexp(float(mant | 0x400), int(exp) - 25);
}

float linearToSrgb(float v) {
    v = std::clamp(v, 0.0f, 1.0f);
    return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1 / 2.4f) - 0.055f;
}

// One pixel of the supported mirror formats, as sRGB.
bool readPixel(DXGI_FORMAT f, const uint8_t* p, float rgb[3]) {
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        rgb[0] = p[0] / 255.0f; rgb[1] = p[1] / 255.0f; rgb[2] = p[2] / 255.0f;
        return true;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        rgb[0] = p[2] / 255.0f; rgb[1] = p[1] / 255.0f; rgb[2] = p[0] / 255.0f;
        return true;
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R10G10B10A2_TYPELESS: {
        uint32_t v;
        std::memcpy(&v, p, 4);
        rgb[0] = (v & 0x3FF) / 1023.0f; rgb[1] = ((v >> 10) & 0x3FF) / 1023.0f; rgb[2] = ((v >> 20) & 0x3FF) / 1023.0f;
        return true;
    }
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS: {
        uint16_t h[3];
        std::memcpy(h, p, 6);
        for (int i = 0; i < 3; ++i) rgb[i] = linearToSrgb(halfToFloat(h[i]));
        return true;
    }
    default: return false;
    }
}

int bytesPerPixel(DXGI_FORMAT f) { return f == DXGI_FORMAT_R16G16B16A16_FLOAT || f == DXGI_FORMAT_R16G16B16A16_TYPELESS ? 8 : 4; }

} // namespace

MirrorSampler::~MirrorSampler() { release(); }

void MirrorSampler::release() {
    if (staging_) { staging_->Release(); staging_ = nullptr; }
    if (mirror_) { mirror_->Release(); mirror_ = nullptr; }
    if (srv_) {
        vr::VRCompositor()->ReleaseMirrorTextureD3D11(srv_);
        srv_ = nullptr;
    }
    stagingW_ = stagingH_ = 0;
}

bool MirrorSampler::open() {
    if (srv_) return true;
    if (util::now() < retryAt_) return false;
    retryAt_ = util::now() + 5.0;
    ID3D11Device* dev = OverlayTexture::sharedDevice();
    if (!dev || !vr::VRCompositor()) return false;
    void* view = nullptr;
    vr::EVRCompositorError e = vr::VRCompositor()->GetMirrorTextureD3D11(vr::Eye_Left, dev, &view);
    if (e != vr::VRCompositorError_None || !view) {
        LOG("mica: no mirror texture (error %d)", int(e));
        return false;
    }
    srv_ = static_cast<ID3D11ShaderResourceView*>(view);
    ID3D11Resource* res = nullptr;
    srv_->GetResource(&res);
    if (!res || FAILED(res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&mirror_))) {
        if (res) res->Release();
        release();
        return false;
    }
    res->Release();
    D3D11_TEXTURE2D_DESC d;
    mirror_->GetDesc(&d);
    texW_ = int(d.Width);
    texH_ = int(d.Height);
    format_ = d.Format;
    float probe[3];
    uint8_t zero[8] = {};
    if (d.SampleDesc.Count != 1 || !readPixel(format_, zero, probe)) {
        LOG("mica: unsupported mirror texture (format %d, %u samples)", int(format_), d.SampleDesc.Count);
        release();
        retryAt_ = util::now() + 1e9; // won't change
        return false;
    }
    vr::VRSystem()->GetProjectionRaw(vr::Eye_Left, &left_, &right_, &top_, &bottom_);
    vr::HmdMatrix34_t eh = vr::VRSystem()->GetEyeToHeadTransform(vr::Eye_Left);
    std::memcpy(eyeToHead_.m, eh.m, sizeof(eyeToHead_.m));
    LOG("mica: mirror %dx%d format %d, tangents l%.2f r%.2f t%.2f b%.2f", texW_, texH_, int(format_), left_, right_, top_, bottom_);
    return true;
}

bool MirrorSampler::sample(const Vec3 corners[4], const Mat34& head, MicaField& out) {
    if (!open()) return false;
    ID3D11DeviceContext* ctx = OverlayTexture::sharedContext();
    if (!ctx) return false;

    // Panel corners -> left eye image (u, v in [0, 1], v down).
    const Mat34 toEye = inverseRigid(head * eyeToHead_);
    const bool yDown = top_ < bottom_;
    float u[4], v[4];
    for (int i = 0; i < 4; ++i) {
        Vec3 p = transformPoint(toEye, corners[i]);
        if (p.z > -0.05f) return false; // behind or too close to the eye
        float tx = p.x / -p.z, ty = p.y / -p.z;
        u[i] = (tx - left_) / (right_ - left_);
        v[i] = ((yDown ? -ty : ty) - top_) / (bottom_ - top_);
    }
    float iu0 = std::min({u[0], u[1], u[2], u[3]}), iu1 = std::max({u[0], u[1], u[2], u[3]});
    float iv0 = std::min({v[0], v[1], v[2], v[3]}), iv1 = std::max({v[0], v[1], v[2], v[3]});
    float mu = (iu1 - iu0) * kMargin, mv = (iv1 - iv0) * kMargin;
    float u0 = iu0 - mu, u1 = iu1 + mu, v0 = iv0 - mv, v1 = iv1 + mv;
    // Pixel region, clamped to the image and to the read-back size.
    int x0 = std::clamp(int(u0 * texW_), 0, texW_), x1 = std::clamp(int(std::ceil(u1 * texW_)), 0, texW_);
    int y0 = std::clamp(int(v0 * texH_), 0, texH_), y1 = std::clamp(int(std::ceil(v1 * texH_)), 0, texH_);
    if (x1 - x0 > kMaxRegion) { int c = (x0 + x1) / 2; x0 = c - kMaxRegion / 2; x1 = x0 + kMaxRegion; }
    if (y1 - y0 > kMaxRegion) { int c = (y0 + y1) / 2; y0 = c - kMaxRegion / 2; y1 = y0 + kMaxRegion; }
    const int rw = x1 - x0, rh = y1 - y0;
    if (rw < 8 || rh < 8) return false; // out of view

    if (!staging_ || stagingW_ < rw || stagingH_ < rh) {
        if (staging_) staging_->Release();
        staging_ = nullptr;
        D3D11_TEXTURE2D_DESC d{};
        d.Width = UINT(std::max(rw, stagingW_));
        d.Height = UINT(std::max(rh, stagingH_));
        d.MipLevels = 1;
        d.ArraySize = 1;
        d.Format = format_;
        d.SampleDesc.Count = 1;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(OverlayTexture::sharedDevice()->CreateTexture2D(&d, nullptr, &staging_))) return false;
        stagingW_ = int(d.Width);
        stagingH_ = int(d.Height);
    }
    D3D11_BOX box{UINT(x0), UINT(y0), 0, UINT(x1), UINT(y1), 1};
    ctx->CopySubresourceRegion(staging_, 0, 0, 0, 0, mirror_, 0, &box);
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(staging_, 0, D3D11_MAP_READ, 0, &m))) return false;

    // Average the area around the panel per cell. The panel itself is left
    // out: the mirror shows the composited image, our own overlays included.
    const float su = float(x1 - x0) / texW_, sv = float(y1 - y0) / texH_;
    const float ru0 = float(x0) / texW_, rv0 = float(y0) / texH_;
    std::vector<float> sum(size_t(kCols) * kRows * 3, 0.0f);
    std::vector<int> count(size_t(kCols) * kRows, 0);
    const int bpp = bytesPerPixel(format_);
    const int step = std::max(1, std::max(rw, rh) / kSamplesPerAxis);
    for (int y = 0; y < rh; y += step) {
        const float fv = rv0 + (y + 0.5f) / texH_;
        const bool insideV = fv > iv0 && fv < iv1;
        const int row = std::clamp(int((float(y) + 0.5f) / rh * kRows), 0, kRows - 1);
        const uint8_t* line = static_cast<const uint8_t*>(m.pData) + size_t(y) * m.RowPitch;
        for (int x = 0; x < rw; x += step) {
            const float fu = ru0 + (x + 0.5f) / texW_;
            if (insideV && fu > iu0 && fu < iu1) continue;
            float c[3];
            readPixel(format_, line + size_t(x) * bpp, c);
            int col = std::clamp(int((float(x) + 0.5f) / rw * kCols), 0, kCols - 1);
            size_t k = size_t(row) * kCols + col;
            sum[k * 3] += c[0]; sum[k * 3 + 1] += c[1]; sum[k * 3 + 2] += c[2];
            ++count[k];
        }
    }
    ctx->Unmap(staging_, 0);

    MicaField f;
    f.cols = kCols;
    f.rows = kRows;
    f.rgb.assign(size_t(kCols) * kRows * 3, 0.0f);
    std::vector<char> known(size_t(kCols) * kRows, 0);
    int knownCount = 0;
    for (size_t k = 0; k < count.size(); ++k)
        if (count[k] > 0) {
            for (int c = 0; c < 3; ++c) f.rgb[k * 3 + c] = sum[k * 3 + c] / float(count[k]);
            known[k] = 1;
            ++knownCount;
        }
    if (!knownCount) return false;
    // Cells hidden by the panel: filled from their neighbours (diffusion).
    std::vector<char> filled = known;
    for (int pass = 0; pass < 60; ++pass) {
        std::vector<float> next = f.rgb;
        for (int r = 0; r < kRows; ++r)
            for (int c = 0; c < kCols; ++c) {
                size_t k = size_t(r) * kCols + c;
                if (known[k]) continue;
                float acc[3] = {0, 0, 0};
                int n = 0;
                const int dr[4] = {-1, 1, 0, 0}, dc[4] = {0, 0, -1, 1};
                for (int i = 0; i < 4; ++i) {
                    int rr = r + dr[i], cc = c + dc[i];
                    if (rr < 0 || rr >= kRows || cc < 0 || cc >= kCols) continue;
                    size_t kk = size_t(rr) * kCols + cc;
                    if (!filled[kk]) continue;
                    for (int ch = 0; ch < 3; ++ch) acc[ch] += f.rgb[kk * 3 + ch];
                    ++n;
                }
                if (n) {
                    for (int ch = 0; ch < 3; ++ch) next[k * 3 + ch] = acc[ch] / float(n);
                    filled[k] = 1;
                }
            }
        f.rgb.swap(next);
    }
    // The panel in grid units.
    f.px0 = (iu0 - ru0) / su * kCols;
    f.px1 = (iu1 - ru0) / su * kCols;
    f.py0 = (iv0 - rv0) / sv * kRows;
    f.py1 = (iv1 - rv0) / sv * kRows;
    out = std::move(f);
    return true;
}
