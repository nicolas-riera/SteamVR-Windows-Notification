#include "vr/toast_overlay.h"

#include <cmath>
#include <cstring>

#include "core/log.h"
#include "gfx/canvas.h"

ToastOverlay::~ToastOverlay() {
    if (handle_ != vr::k_ulOverlayHandleInvalid && vr::VROverlay()) vr::VROverlay()->DestroyOverlay(handle_);
}

bool ToastOverlay::create(const std::string& key) {
    vr::IVROverlay* ov = vr::VROverlay();
    vr::EVROverlayError e = ov->CreateOverlay(key.c_str(), "Windows notification", &handle_);
    if (e != vr::VROverlayError_None) {
        LOG("overlay: CreateOverlay(%s) failed: %s", key.c_str(), ov->GetOverlayErrorNameFromEnum(e));
        handle_ = vr::k_ulOverlayHandleInvalid;
        return false;
    }
    ov->SetOverlayInputMethod(handle_, vr::VROverlayInputMethod_None);
    // Above other overlays, and still drawn while the SteamVR menu is open
    // (we decide ourselves when to hide it).
    ov->SetOverlaySortOrder(handle_, 200);
    ov->SetOverlayFlag(handle_, vr::VROverlayFlags_VisibleInDashboard, true);
    ov->SetOverlayFlag(handle_, vr::VROverlayFlags_NoBackside, false);
    return true;
}

void ToastOverlay::upload(const Canvas& c) {
    if (handle_ != vr::k_ulOverlayHandleInvalid) texture_.upload(handle_, c);
}

void ToastOverlay::place(const Mat34& pose, float widthMeters, float alpha) {
    if (handle_ == vr::k_ulOverlayHandleInvalid) return;
    if (alpha < 0.004f) {
        hide();
        return;
    }
    vr::IVROverlay* ov = vr::VROverlay();
    vr::HmdMatrix34_t m;
    std::memcpy(m.m, pose.m, sizeof(m.m));
    ov->SetOverlayTransformAbsolute(handle_, vr::TrackingUniverseStanding, &m);
    if (std::fabs(widthMeters - width_) > 1e-5f) ov->SetOverlayWidthInMeters(handle_, width_ = widthMeters);
    if (std::fabs(alpha - alpha_) > 1e-3f) ov->SetOverlayAlpha(handle_, alpha_ = alpha);
    if (!shown_) {
        ov->ShowOverlay(handle_);
        shown_ = true;
    }
}

void ToastOverlay::hide() {
    if (shown_ && handle_ != vr::k_ulOverlayHandleInvalid) vr::VROverlay()->HideOverlay(handle_);
    shown_ = false;
}
