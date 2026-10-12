#pragma once
// One world-locked SteamVR overlay showing one notification. Non-interactive:
// no input method, so lasers and controllers go straight through it.
#include <openvr.h>

#include <string>

#include "core/mathx.h"
#include "vr/overlay_texture.h"

class Canvas;

class ToastOverlay {
public:
    ToastOverlay() = default;
    ~ToastOverlay();
    ToastOverlay(const ToastOverlay&) = delete;
    ToastOverlay& operator=(const ToastOverlay&) = delete;

    // Higher sort orders are drawn on top (content above its backdrop).
    bool create(const std::string& key, uint32_t sortOrder);
    void upload(const Canvas& c);
    // Absolute (standing) pose of the overlay center; hidden when alpha ~ 0.
    void place(const Mat34& pose, float widthMeters, float alpha);
    void hide();

private:
    vr::VROverlayHandle_t handle_ = vr::k_ulOverlayHandleInvalid;
    OverlayTexture texture_;
    bool shown_ = false;
    float alpha_ = -1, width_ = -1;
};
