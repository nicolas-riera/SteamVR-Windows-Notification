#include "vr/vr_input.h"

#include "core/log.h"

bool VrInput::init(const std::string& manifest) {
    vr::IVRInput* in = vr::VRInput();
    if (!in) return false;
    vr::EVRInputError e = in->SetActionManifestPath(manifest.c_str());
    if (e != vr::VRInputError_None) {
        LOG("input: SetActionManifestPath failed (%d)", int(e));
        return false;
    }
    in->GetActionSetHandle("/actions/swn", &set_);
    in->GetActionHandle("/actions/swn/in/activate", &activate_);
    in->GetActionHandle("/actions/swn/in/pointer", &pointer_);
    in->GetInputSourceHandle("/user/hand/left", &hands_[0]);
    in->GetInputSourceHandle("/user/hand/right", &hands_[1]);
    ok_ = set_ != vr::k_ulInvalidActionSetHandle && activate_ != vr::k_ulInvalidActionHandle;
    return ok_;
}

void VrInput::update() {
    edge_[0] = edge_[1] = false;
    if (!ok_) return;
    vr::VRActiveActionSet_t active{};
    active.ulActionSet = set_;
    if (vr::VRInput()->UpdateActionState(&active, sizeof(active), 1) != vr::VRInputError_None) return;
    for (int hand = 0; hand < 2; ++hand) {
        vr::InputDigitalActionData_t d{};
        bool state = vr::VRInput()->GetDigitalActionData(activate_, &d, sizeof(d), hands_[hand]) == vr::VRInputError_None &&
                     d.bActive && d.bState;
        edge_[hand] = primed_ && state && !prev_[hand];
        prev_[hand] = state;
    }
    primed_ = true;
}

bool VrInput::pointerRay(int hand, float o[3], float d[3]) const {
    if (!ok_ || pointer_ == vr::k_ulInvalidActionHandle) return false;
    vr::InputPoseActionData_t p{};
    if (vr::VRInput()->GetPoseActionDataRelativeToNow(pointer_, vr::TrackingUniverseStanding, 0, &p, sizeof(p), hands_[hand]) !=
            vr::VRInputError_None ||
        !p.bActive || !p.pose.bPoseIsValid)
        return false;
    const auto& m = p.pose.mDeviceToAbsoluteTracking.m;
    for (int i = 0; i < 3; ++i) {
        o[i] = m[i][3];
        d[i] = -m[i][2]; // pointing direction is -Z
    }
    return true;
}
