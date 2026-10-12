#pragma once
// SteamVR Input: a "click" button (trigger by default) and a pointer pose per
// hand, to notice a controller pointing at a notification and clicking.
// Only read while a notification is shown; the game keeps its input.
#include <openvr.h>

#include <string>

class VrInput {
public:
    bool init(const std::string& actionManifestPath);
    void update();
    // Rising edge since the previous update(). hand: 0 = left, 1 = right.
    bool clicked(int hand) const { return edge_[hand]; }
    bool pointerRay(int hand, float origin[3], float dir[3]) const;
    bool ok() const { return ok_; }

private:
    bool ok_ = false;
    vr::VRActionSetHandle_t set_ = vr::k_ulInvalidActionSetHandle;
    vr::VRActionHandle_t activate_ = vr::k_ulInvalidActionHandle;
    vr::VRActionHandle_t pointer_ = vr::k_ulInvalidActionHandle;
    vr::VRInputValueHandle_t hands_[2] = {vr::k_ulInvalidInputValueHandle, vr::k_ulInvalidInputValueHandle};
    bool prev_[2] = {false, false};
    bool edge_[2] = {false, false};
};
