#include "app/presenter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>

#include "core/log.h"
#include "core/settings.h"
#include "core/util.h"
#include "gfx/font.h"
#include "notify/system_state.h"
#include "notify/wpn_store.h"

namespace {

constexpr double kImageWait = 1.5;     // s, max delay of a toast waiting for a remote image
constexpr double kEnterTime = 0.30;    // s, fade in
constexpr double kLeaveTime = 0.30;    // s, fade out
constexpr float kGroupFadeTime = 0.22f;// s, fade when the SteamVR menu hides / shows them
constexpr float kStackTime = 0.22f;    // s, stack re-layout glide
constexpr float kStackGap = 0.012f;    // m, between stacked notifications
constexpr float kDockGap = 0.008f;     // m, between the menu bar and a docked notification
constexpr float kDockLift = 0.015f;    // m, in front of the menu, so it is never behind it
constexpr float kDockAngle = 30.0f;    // degrees: docks when you face the menu within this
constexpr float kUndockAngle = 45.0f;  // and detaches past this
constexpr size_t kMaxNormalQueue = 20;
constexpr double kTrimAfter = 30.0;    // s without notifications before freeing the GPU device
constexpr int kMaxPriority = 3;
constexpr double kForever = std::numeric_limits<double>::infinity();

bool icontains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return false;
    auto it = std::search(hay.begin(), hay.end(), needle.begin(), needle.end(),
                          [](char a, char b) { return tolower((unsigned char)a) == tolower((unsigned char)b); });
    return it != hay.end();
}

Mat34 toMat(const vr::HmdMatrix34_t& m) {
    Mat34 r;
    std::memcpy(r.m, m.m, sizeof(r.m));
    return r;
}

Vec3 position(const Mat34& m) { return {m.m[0][3], m.m[1][3], m.m[2][3]}; }

// Overlay pose at `p` facing `eye`, kept upright.
Mat34 facing(const Vec3& p, const Vec3& eye) {
    Vec3 z = (eye - p).normalized();
    Vec3 x = Vec3{0, 1, 0}.cross(z);
    if (x.length() < 1e-3f) x = {1, 0, 0};
    x = x.normalized();
    Vec3 y = z.cross(x);
    Mat34 m;
    m.m[0][0] = x.x; m.m[0][1] = y.x; m.m[0][2] = z.x; m.m[0][3] = p.x;
    m.m[1][0] = x.y; m.m[1][1] = y.y; m.m[1][2] = z.y; m.m[1][3] = p.y;
    m.m[2][0] = x.z; m.m[2][1] = y.z; m.m[2][2] = z.z; m.m[2][3] = p.z;
    return m;
}

bool isPriority(Scenario s) { return s != Scenario::Default; }

} // namespace

Presenter::Presenter(const Settings& settings, FontSet& fonts, WpnStore& store)
    : settings_(settings), fonts_(fonts), store_(store), renderer_(fonts) {
    dashboard_ = vr::VROverlay()->IsDashboardVisible();
}

Presenter::~Presenter() = default;

// ---------------------------------------------------------------- database

bool Presenter::allowed(const Toast& t) {
    for (const std::string& ex : settings_.excludedApps)
        if (icontains(t.aumid, ex)) return false;
    if (!sys::toastsEnabled() || !store_.bannerAllowed(t.aumid)) return false;
    if (settings_.respectDoNotDisturb) {
        int profile = sys::quietHoursProfile();
        if (profile == 2) return t.scenario == Scenario::Alarm;  // alarms only
        if (profile != 0) return isPriority(t.scenario);         // priority only / Do Not Disturb
    }
    return true;
}

double Presenter::duration(const Toast& t) const {
    // Reminders, alarms and calls with a button stay on Windows until acted
    // upon. Closing their banner leaves no trace other apps can see, so they
    // only stay until removed from Windows if the user asked for it.
    bool persistent = (t.scenario == Scenario::Reminder || t.scenario == Scenario::Alarm || t.scenario == Scenario::IncomingCall) &&
                      t.hasActions;
    if (persistent && settings_.keepReminders) return kForever;
    if (t.longDuration || persistent) return settings_.longDuration;
    return settings_.shortDuration > 0 ? settings_.shortDuration : sys::messageDuration();
}

void Presenter::syncDatabase(double now) {
    for (ToastRow& row : store_.fetchNew()) {
        Toast t;
        if (!parseToast(row.payload, store_.data(row.id), t)) {
            LOG("toast %lld: unreadable payload", (long long)row.id);
            continue;
        }
        t.id = row.id;
        t.order = row.order;
        t.aumid = row.aumid;
        t.tag = row.tag;
        t.group = row.group;

        // Replacement of a toast we know (same id, or same app + tag + group).
        Entry* same = nullptr;
        for (auto& e : entries_)
            if (e->state != State::Leaving &&
                (e->toast.id == t.id || (!t.tag.empty() && e->toast.aumid == t.aumid && e->toast.tag == t.tag && e->toast.group == t.group)))
                same = e.get();
        if (same) {
            bool changed = !same->toast.sameContent(t);
            same->toast = t;
            same->payload = row.payload;
            if (changed) {
                loadImages(*same);
                same->dirty = true;
            }
            // Windows pops the updated banner again.
            if (same->state == State::Showing) same->expiresAt = now + duration(t);
            LOG("toast %lld: updated (%s)", (long long)t.id, t.aumid.c_str());
            continue;
        }
        if (!allowed(t)) {
            LOG("toast %lld from %s: no banner (settings / Do Not Disturb)", (long long)t.id, t.aumid.c_str());
            continue;
        }
        auto e = std::make_unique<Entry>();
        e->toast = std::move(t);
        e->payload = std::move(row.payload);
        e->priority = isPriority(e->toast.scenario);
        e->arrived = now;
        const AppInfo& app = apps_.lookup(e->toast.aumid, store_, ToastRenderer::iconPx(settings_.renderScale));
        e->visual.appName = app.name;
        e->visual.appIcon = app.icon;
        loadImages(*e);
        LOG("toast %lld from %s: \"%s\"%s", (long long)e->toast.id, e->toast.aumid.c_str(), e->toast.title.c_str(),
            e->priority ? " (priority)" : "");
        entries_.push_back(std::move(e));
    }

    // Dismissed in Windows: gone from the database.
    std::set<int64_t> ids = store_.toastIds();
    for (auto& e : entries_) {
        if (e->state == State::Leaving || ids.count(e->toast.id)) continue;
        LOG("toast %lld: removed from Windows", (long long)e->toast.id);
        if (e->state == State::Showing) {
            e->state = State::Leaving;
            e->leaveAt = now;
        } else {
            e->state = State::Leaving;
            e->leaveAt = -kLeaveTime; // never shown: dropped right away
        }
    }

    // Progress bars and other {bindings} updated by the app.
    for (auto& e : entries_) {
        if (e->state != State::Showing || e->payload.find('{') == std::string::npos) continue;
        Toast t;
        if (!parseToast(e->payload, store_.data(e->toast.id), t)) continue;
        if (t.sameContent(e->toast)) continue;
        t.id = e->toast.id; t.order = e->toast.order; t.aumid = e->toast.aumid; t.tag = e->toast.tag; t.group = e->toast.group;
        e->toast = std::move(t);
        e->dirty = true;
    }
}

bool Presenter::loadImage(const ToastImage& img, const std::string& aumid, int maxW, int maxH, Image& out) {
    out = Image{};
    if (img.empty()) return true;
    std::wstring path;
    if (isRemoteUri(img.src)) {
        if (!downloader_.get(img.src, path)) return false; // still downloading
    } else {
        path = resolveLocalImage(img.src, aumid);
    }
    if (!path.empty() && !::loadImage(path, maxW, maxH, out)) LOG("image: cannot decode %s", util::toUtf8(path).c_str());
    return true;
}

void Presenter::loadImages(Entry& e) {
    const float s = settings_.renderScale;
    const std::string& id = e.toast.aumid;
    const int logo = ToastRenderer::logoPx(s) * 2, wide = ToastRenderer::heroMaxPx(s);
    bool ok = loadImage(e.toast.logo, id, logo, logo, e.visual.logo);
    ok &= loadImage(e.toast.hero, id, wide, wide, e.visual.hero);
    ok &= loadImage(e.toast.inlineImage, id, wide, wide, e.visual.inlineImage);
    e.imagesPending = !ok;
}

void Presenter::imagesArrived(double /*now*/) {
    for (auto& e : entries_) {
        if (!e->imagesPending || e->state == State::Leaving) continue;
        loadImages(*e);
        if (e->state == State::Showing) e->dirty = true;
    }
}

// ---------------------------------------------------------------- lifecycle

void Presenter::render(Entry& e) {
    bool light = settings_.theme == "light" || (settings_.theme == "auto" && sys::lightTheme());
    Theme theme = Theme::make(light, sys::accentColor());
    e.visual.toast = e.toast;
    renderer_.render(e.visual, theme, settings_.renderScale, settings_.opacity, e.canvas);
    if (e.overlay) e.overlay->upload(e.canvas);
    const float mpp = settings_.width / (ToastRenderer::kWidth * settings_.renderScale);
    e.heightM = (float(e.canvas.height()) - 2 * ToastRenderer::kShadowPad * settings_.renderScale) * mpp;
    e.dirty = false;
}

void Presenter::show(Entry& e, double now) {
    e.overlay = std::make_unique<ToastOverlay>();
    if (!e.overlay->create("swn.toast." + std::to_string(++overlayCounter_))) e.overlay.reset();
    render(e);
    e.state = State::Showing;
    e.shownAt = now;
    e.expiresAt = now + duration(e.toast);
    LOG("toast %lld: shown for %.1f s", (long long)e.toast.id, e.expiresAt - now);
}

void Presenter::lifecycle(double now) {
    int normalShown = 0, priorityShown = 0;
    for (auto& e : entries_) {
        if (e->state == State::Showing && now >= e->expiresAt) {
            e->state = State::Leaving;
            e->leaveAt = now;
        }
        if (e->state == State::Waiting && (!e->imagesPending || now - e->arrived > kImageWait)) e->state = State::Queued;
        if (e->state == State::Showing) ++(e->priority ? priorityShown : normalShown);
    }
    // Finished leaving.
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const std::unique_ptr<Entry>& e) { return e->state == State::Leaving && now - e->leaveAt >= kLeaveTime; }),
                   entries_.end());
    // Like Windows, normal banners queue up when too many arrive at once.
    size_t queued = 0;
    for (auto& e : entries_)
        if (e->state == State::Queued && !e->priority) ++queued;
    for (auto it = entries_.begin(); queued > kMaxNormalQueue && it != entries_.end();) {
        if ((*it)->state == State::Queued && !(*it)->priority) { it = entries_.erase(it); --queued; }
        else ++it;
    }
    // Arrival order: entries_ is sorted by arrival.
    for (auto& e : entries_) {
        if (e->state != State::Queued) continue;
        if (e->priority ? priorityShown >= kMaxPriority : normalShown >= 1) continue;
        show(*e, now);
        ++(e->priority ? priorityShown : normalShown);
    }
    for (auto& e : entries_)
        if (e->state == State::Showing && e->dirty) render(*e);
}

// ---------------------------------------------------------------- placement

void Presenter::setDashboard(bool open) {
    if (open == dashboard_) return;
    dashboard_ = open;
    nextTabPoll_ = 0;
    desktopTab_ = false;
}

Presenter::Mode Presenter::mode(double now) {
    if (!dashboard_) return Mode::Follow;
    if (now >= nextTabPoll_) {
        // The Desktop tab shows the "system.desktop.N" overlays; they are only
        // visible while that tab is selected.
        nextTabPoll_ = now + 0.15;
        desktopTab_ = false;
        for (int i = 0; i < 8 && !desktopTab_; ++i) {
            vr::VROverlayHandle_t h = vr::k_ulOverlayHandleInvalid;
            std::string key = "system.desktop." + std::to_string(i);
            if (vr::VROverlay()->FindOverlay(key.c_str(), &h) == vr::VROverlayError_None)
                desktopTab_ = vr::VROverlay()->IsOverlayVisible(h);
        }
    }
    return desktopTab_ ? Mode::Hidden : Mode::Dashboard;
}

bool Presenter::headPose(Mat34& head) {
    vr::IVRSystem* sys = vr::VRSystem();
    float sinceVsync = 0;
    sys->GetTimeSinceLastVsync(&sinceVsync, nullptr);
    float hz = sys->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_DisplayFrequency_Float);
    float toPhotons = sys->GetFloatTrackedDeviceProperty(vr::k_unTrackedDeviceIndex_Hmd, vr::Prop_SecondsFromVsyncToPhotons_Float);
    float predict = hz > 1 ? std::clamp(1.0f / hz - sinceVsync + toPhotons, 0.0f, 0.05f) : 0.0f;
    vr::TrackedDevicePose_t pose;
    sys->GetDeviceToAbsoluteTrackingPose(vr::TrackingUniverseStanding, predict, &pose, 1);
    if (!pose.bPoseIsValid) return false;
    head = toMat(pose.mDeviceToAbsoluteTracking);
    return true;
}

// The SteamVR menu's bottom bar (Steam's: clock, battery...): its width
// follows its content, so its right end is found from the texture's alpha.
bool Presenter::barTopRight(double now, Mat34& corner) {
    vr::IVROverlay* ov = vr::VROverlay();
    vr::VROverlayHandle_t bar = vr::k_ulOverlayHandleInvalid;
    if (ov->FindOverlay("valve.steam.gamepadui.bar", &bar) != vr::VROverlayError_None || !ov->IsOverlayVisible(bar)) return false;
    vr::HmdVector2_t size{};
    if (ov->GetOverlayMouseScale(bar, &size) != vr::VROverlayError_None || size.v[0] < 1 || size.v[1] < 1) return false;
    if (now >= nextBarScan_) {
        nextBarScan_ = now + 1.0;
        uint32_t w = 0, h = 0;
        std::vector<uint8_t> px(size_t(size.v[0]) * size_t(size.v[1]) * 4);
        barRight_ = size.v[0];
        if (ov->GetOverlayImageData(bar, px.data(), uint32_t(px.size()), &w, &h) == vr::VROverlayError_None && w && h &&
            size_t(w) * h * 4 <= px.size()) {
            for (int x = int(w) - 1; x >= 0; --x) {
                bool opaque = false;
                for (uint32_t y = 0; y < h && !opaque; y += 2) opaque = px[(size_t(y) * w + x) * 4 + 3] > 40;
                if (opaque) {
                    barRight_ = float(x + 1) * size.v[0] / float(w);
                    break;
                }
            }
        }
    }
    // Overlay coordinates: origin at the bottom-left, so y = height is the top edge.
    vr::HmdMatrix34_t m;
    if (ov->GetTransformForOverlayCoordinates(bar, vr::TrackingUniverseStanding, {barRight_, size.v[1]}, &m) != vr::VROverlayError_None)
        return false;
    corner = toMat(m);
    return position(corner).length() > 1e-4f;
}

void Presenter::place(double now, float dt) {
    Mode m = mode(now);
    Mat34 head;
    if (!headPose(head)) return;
    const Vec3 eye = position(head);
    Vec3 fwd{-head.m[0][2], -head.m[1][2], -head.m[2][2]};
    float headYaw, headPitch;
    toYawPitch(fwd, headYaw, headPitch);
    // Looking straight up or down: the heading is meaningless, keep it.
    if (std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z) < 0.25f) headYaw = lastYaw_;
    lastYaw_ = headYaw;

    // Newest notification on screen (the group's anchor).
    Entry* newest = nullptr;
    for (auto& e : entries_)
        if (e->state == State::Showing && (!newest || e->shownAt > newest->shownAt)) newest = e.get();

    // Where the group wants to be: below the gaze, or with the SteamVR menu
    // open, just above the right end of its bottom bar (as long as you face it).
    Vec3 target = eye + fromYawPitch(headYaw, std::clamp(headPitch + rad(settings_.pitch), rad(-75.0f), rad(60.0f))) * settings_.distance;
    bool docked = false;
    Mat34 corner;
    if (m == Mode::Dashboard && barTopRight(now, corner)) {
        const Vec3 cornerPos = position(corner);
        Vec3 right = Vec3{corner.m[0][0], corner.m[1][0], corner.m[2][0]}.normalized();
        Vec3 up = Vec3{corner.m[0][1], corner.m[1][1], corner.m[2][1]}.normalized();
        Vec3 normal = Vec3{corner.m[0][2], corner.m[1][2], corner.m[2][2]}.normalized();
        float h = newest ? newest->heightM : 0.1f;
        Vec3 dock = cornerPos + up * (kDockGap + h * 0.5f) - right * (settings_.width * 0.5f) + normal * kDockLift;
        // Detaches when you look away from the menu, docks again when you face it.
        float dockYaw, dockPitch;
        toYawPitch(dock - eye, dockYaw, dockPitch);
        float off = std::fabs(deg(wrapAngle(dockYaw - headYaw)));
        if (docked_ && off > kUndockAngle) docked_ = false;
        else if (!docked_ && off < kDockAngle) docked_ = true;
        if (docked_) {
            target = dock;
            docked = true;
        }
    } else {
        docked_ = true; // docks right away the next time the menu opens
    }

    // Fade in / out with the SteamVR menu's Desktop tab.
    float wantAlpha = m == Mode::Hidden ? 0.0f : 1.0f;
    if (groupAlpha_ <= 0.0f && wantAlpha > 0.0f) placed_ = false; // reappears in place
    groupAlpha_ += std::clamp(wantAlpha - groupAlpha_, -dt / kGroupFadeTime, dt / kGroupFadeTime);

    if (fresh_) {
        // First notification on screen: no fade of the group, only its own.
        groupAlpha_ = wantAlpha;
        fresh_ = false;
    }
    if (!placed_) {
        pos_ = target;
        placed_ = true;
        chasing_ = false;
        yawVel_ = pitchVel_ = distVel_ = 0;
    } else {
        // Spherical coordinates around the eyes, so a glide arcs around you
        // instead of cutting through your head.
        float cy, cp, ty, tp;
        Vec3 cur = pos_ - eye, tgt = target - eye;
        toYawPitch(cur, cy, cp);
        toYawPitch(tgt, ty, tp);
        float cd = cur.length(), td = tgt.length();
        float dy = wrapAngle(ty - cy), dp = tp - cp, dd = td - cd;
        // Docked: always on its spot. Otherwise, still while within the cone.
        if (!chasing_ && ((docked && (cur - tgt).length() > 0.002f) || std::fabs(deg(dy)) > settings_.deadzoneYaw ||
                          std::fabs(deg(dp)) > settings_.deadzonePitch || std::fabs(dd) > 0.2f * td))
            chasing_ = true;
        if (chasing_) {
            float ny = smoothDamp(cy, cy + dy, yawVel_, settings_.followTime, dt);
            float np = smoothDamp(cp, tp, pitchVel_, settings_.followTime, dt);
            float nd = smoothDamp(cd, td, distVel_, settings_.followTime, dt);
            pos_ = eye + fromYawPitch(ny, np) * nd;
            bool arrived = std::fabs(deg(wrapAngle(ty - ny))) < 0.2f && std::fabs(deg(tp - np)) < 0.2f && std::fabs(td - nd) < 0.003f &&
                           std::fabs(yawVel_) < 0.01f && std::fabs(pitchVel_) < 0.01f;
            if (arrived) {
                chasing_ = false;
                yawVel_ = pitchVel_ = distVel_ = 0;
                if (docked) pos_ = target;
            }
        }
    }

    // Stack: newest at the bottom (on the anchor), older ones above.
    std::vector<Entry*> stack;
    for (auto& e : entries_)
        if (e->state == State::Showing) stack.push_back(e.get());
    std::sort(stack.begin(), stack.end(), [](Entry* a, Entry* b) { return a->shownAt > b->shownAt; });
    float bottom = 0; // bottom edge of the next one up
    bool moving = false;
    for (size_t i = 0; i < stack.size(); ++i) {
        Entry& e = *stack[i];
        float want = i == 0 ? 0.0f : bottom + e.heightM * 0.5f;
        bottom = want + e.heightM * 0.5f + kStackGap;
        if (!e.yPlaced) { e.y = want; e.yPlaced = true; }
        e.y = smoothDamp(e.y, want, e.yVel, kStackTime, dt);
        moving |= std::fabs(e.y - want) > 1e-4f;
    }

    // Overlay poses: notifications appear and disappear in place, fading.
    const Mat34 group = facing(pos_, eye);
    const Vec3 up{group.m[0][1], group.m[1][1], group.m[2][1]};
    const float mpp = settings_.width / (ToastRenderer::kWidth * settings_.renderScale);
    const float ga = easeInOutCubic(std::clamp(groupAlpha_, 0.0f, 1.0f));
    bool animating = moving || chasing_ || groupAlpha_ != wantAlpha;
    for (auto& e : entries_) {
        if (!e->overlay || (e->state != State::Showing && e->state != State::Leaving)) continue;
        float alpha = easeInOutCubic(std::clamp(float((now - e->shownAt) / kEnterTime), 0.0f, 1.0f));
        if (e->state == State::Leaving) alpha *= 1 - easeInOutCubic(std::clamp(float((now - e->leaveAt) / kLeaveTime), 0.0f, 1.0f));
        animating |= now - e->shownAt < kEnterTime || e->state == State::Leaving;
        Vec3 p = pos_ + up * e->y;
        Mat34 pose = group;
        pose.m[0][3] = p.x; pose.m[1][3] = p.y; pose.m[2][3] = p.z;
        e->overlay->place(pose, float(e->canvas.width()) * mpp, alpha * ga);
    }
    pace_ = animating ? Pace::Frame : Pace::Watch;
}

void Presenter::tick(double now) {
    float dt = lastTick_ > 0 ? float(std::clamp(now - lastTick_, 0.0, 0.1)) : 0.0f;
    lastTick_ = now;
    lifecycle(now);
    bool any = false;
    for (auto& e : entries_) any |= e->state == State::Showing || e->state == State::Leaving;
    if (!any) {
        // Nothing on screen: the next notification appears in front of you.
        placed_ = false;
        fresh_ = true;
        bool waiting = false;
        for (auto& e : entries_) waiting |= e->state == State::Waiting;
        pace_ = waiting ? Pace::Watch : Pace::Idle;
        if (idleSince_ < 0) idleSince_ = now;
        if (!trimmed_ && entries_.empty() && now - idleSince_ > kTrimAfter) {
            // Long idle: give back the GPU device and the memory pages.
            OverlayTexture::releaseDevice();
            SetProcessWorkingSetSize(GetCurrentProcess(), SIZE_T(-1), SIZE_T(-1));
            trimmed_ = true;
        }
        return;
    }
    idleSince_ = -1;
    trimmed_ = false;
    place(now, dt);
}
