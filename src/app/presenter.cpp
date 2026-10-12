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

constexpr double kImageWait = 1.5;      // s, max delay of a toast waiting for a remote image
constexpr double kEnterTime = 0.30;     // s, fade in
constexpr double kLeaveTime = 0.30;     // s, fade out
constexpr float kGroupFadeTime = 0.22f; // s, fade when the SteamVR menu hides / shows them
constexpr float kStackTime = 0.22f;     // s, stack re-layout glide
constexpr float kStackGap = 0.012f;     // m, between stacked notifications
constexpr float kDockGap = 0.03f;       // m, between the menu bar and a docked notification
constexpr float kDockLift = 0.03f;      // m, in front of the menu, so it is never behind it
constexpr float kDockTilt = 25.0f;     // degrees, docked ones lean back towards you (the menu window is curved)
constexpr float kDockAngle = 30.0f;     // degrees: docks when you face the menu within this
constexpr float kUndockAngle = 45.0f;   // and detaches past this
constexpr double kMicaInterval = 0.2;   // s, between two looks at the scene behind
constexpr float kMicaBlend = 0.45f;     // share of a new look in the Mica colors (smooths changes)
constexpr float kFallbackOpacity = 0.85f; // background without Mica
constexpr float kFooterTime = 0.28f;    // s, footer slide
constexpr double kFooterShown = 5.0;   // s, footer (and notification) kept after a click
constexpr float kFooterGap = 0.006f;   // m, between a notification and its footer
constexpr int64_t kExpirySlack = 20000000; // 2 s in FILETIME units: removed this close to its expiry = expired
constexpr double kBannerExtra = 1.0;   // s, Windows keeps a banner ~1 s longer than its setting (measured)
constexpr size_t kMaxNormalQueue = 20;
constexpr double kTrimAfter = 30.0;     // s without notifications before freeing the GPU device
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
Vec3 axis(const Mat34& m, int i) { return Vec3{m.m[0][i], m.m[1][i], m.m[2][i]}.normalized(); }

Mat34 basis(const Vec3& x, const Vec3& y, const Vec3& z, const Vec3& p) {
    Mat34 m;
    m.m[0][0] = x.x; m.m[0][1] = y.x; m.m[0][2] = z.x; m.m[0][3] = p.x;
    m.m[1][0] = x.y; m.m[1][1] = y.y; m.m[1][2] = z.y; m.m[1][3] = p.y;
    m.m[2][0] = x.z; m.m[2][1] = y.z; m.m[2][2] = z.z; m.m[2][3] = p.z;
    return m;
}

bool isPriority(Scenario s) { return s != Scenario::Default; }

} // namespace

Presenter::Presenter(const Settings& settings, FontSet& fonts, WpnStore& store, const std::string& actionManifest)
    : settings_(settings), fonts_(fonts), store_(store), renderer_(fonts) {
    dashboard_ = vr::VROverlay()->IsDashboardVisible();
    if (!input_.init(actionManifest)) LOG("input: unavailable, notifications cannot be clicked");
}

Presenter::~Presenter() {
    entries_.clear();
    sampler_.release();
}

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
    return settings_.shortDuration > 0 ? settings_.shortDuration : sys::messageDuration() + kBannerExtra;
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
        t.expiry = row.expiry;

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
        LOG("toast %lld from %s: \"%s\"%s, expires in %.0f s", (long long)e->toast.id, e->toast.aumid.c_str(), e->toast.title.c_str(),
            e->priority ? " (priority)" : "", double(row.expiry - row.arrival) / 1e7);
        entries_.push_back(std::move(e));
    }

    // Gone from the database. Measured on the desktop:
    //  - expired: Windows still shows its banner (even when it was queued);
    //  - removed (by you, or hidden by its app): a queued banner is skipped,
    //    a shown one disappears at once and the next one takes its place.
    std::set<int64_t> ids = store_.toastIds();
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    const int64_t nowFt = (int64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    for (auto& e : entries_) {
        if (e->state == State::Leaving || e->removed || ids.count(e->toast.id)) continue;
        e->removed = true;
        if (e->toast.expiry > 0 && nowFt >= e->toast.expiry - kExpirySlack) {
            LOG("toast %lld: expired in Windows, banner kept", (long long)e->toast.id);
            continue;
        }
        LOG("toast %lld: removed from Windows", (long long)e->toast.id);
        leave(*e, now, true); // as on Windows: gone at once, the next one shows
    }

    // Progress bars and other {bindings} updated by the app.
    for (auto& e : entries_) {
        if (e->state != State::Showing || e->payload.find('{') == std::string::npos) continue;
        Toast t;
        if (!parseToast(e->payload, store_.data(e->toast.id), t)) continue;
        if (t.sameContent(e->toast)) continue;
        t.id = e->toast.id; t.order = e->toast.order; t.aumid = e->toast.aumid; t.tag = e->toast.tag; t.group = e->toast.group;
        t.expiry = e->toast.expiry;
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
    e.theme = Theme::make(light, sys::accentColor());
    e.visual.toast = e.toast;
    e.panelH = renderer_.render(e.visual, e.theme, settings_.renderScale, e.canvas);
    if (e.overlay) e.overlay->upload(e.canvas);
    e.heightM = e.panelH * settings_.width / ToastRenderer::kWidth;
    e.dirty = false;
    renderBackdrop(e);
}

// The backdrop is drawn at half the content's resolution: Mica is a blur.
void Presenter::renderBackdrop(Entry& e) {
    const MicaField* field = settings_.mica && e.field.valid() ? &e.field : nullptr;
    renderer_.renderBackdrop(field, e.theme, e.panelH, settings_.renderScale * 0.5f, e.canvas.width() / 2, e.canvas.height() / 2,
                             kFallbackOpacity, e.backCanvas);
    if (e.backdrop) e.backdrop->upload(e.backCanvas);
}

void Presenter::show(Entry& e, double now) {
    const std::string key = "swn.toast." + std::to_string(++overlayCounter_);
    e.backdrop = std::make_unique<ToastOverlay>();
    if (!e.backdrop->create(key + ".backdrop", 199)) e.backdrop.reset();
    e.overlay = std::make_unique<ToastOverlay>();
    if (!e.overlay->create(key, 200)) e.overlay.reset();
    render(e);
    e.state = State::Showing;
    e.shownAt = std::max(now, util::now()); // after the render: its time is not taken from the display
    e.expiresAt = e.shownAt + duration(e.toast);
    e.micaDue = 0; // looks at the scene before its first frame
    LOG("toast %lld: shown for %.1f s", (long long)e.toast.id, e.expiresAt - e.shownAt);
}

void Presenter::leave(Entry& e, double now, bool instant) {
    if (e.state == State::Leaving) return;
    // Never shown, or instant: dropped right away (its overlays go with it).
    e.leaveAt = e.state == State::Showing && !instant ? now : -kForever;
    e.state = State::Leaving;
}

void Presenter::lifecycle(double now) {
    // On the Desktop tab of the SteamVR menu you see the real banner: what
    // is shown or waiting counts as seen, and nothing new shows meanwhile.
    if (dashboard_ && desktopTab_) {
        for (auto& e : entries_)
            if (e->state != State::Leaving) {
                LOG("toast %lld: seen on the desktop", (long long)e->toast.id);
                leave(*e, now);
            }
    }
    int normalShown = 0, priorityShown = 0;
    for (auto& e : entries_) {
        if (e->state == State::Showing && now >= e->expiresAt) leave(*e, now);
        if (e->state == State::Waiting && (!e->imagesPending || now - e->arrived > kImageWait)) e->state = State::Queued;
        // A leaving one keeps its place until faded out: the next waits for it.
        bool occupies = e->state == State::Showing || (e->state == State::Leaving && e->leaveAt > -kForever);
        if (occupies) ++(e->priority ? priorityShown : normalShown);
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

// ---------------------------------------------------------------- SteamVR menu

void Presenter::setDashboard(bool open) {
    if (open == dashboard_) return;
    dashboard_ = open;
    nextTabPoll_ = 0;
    desktopTab_ = false;
}

void Presenter::pollDesktopTab(double now) {
    if (!dashboard_) {
        desktopTab_ = false;
        return;
    }
    if (now < nextTabPoll_) return;
    // The Desktop tab shows the "system.desktop.N" overlays; they are only
    // visible while that tab is selected.
    nextTabPoll_ = now + 0.15;
    desktopTab_ = false;
    for (int i = 0; i < 8 && !desktopTab_; ++i) {
        vr::VROverlayHandle_t h = vr::k_ulOverlayHandleInvalid;
        std::string key = "system.desktop." + std::to_string(i);
        if (vr::VROverlay()->FindOverlay(key.c_str(), &h) == vr::VROverlayError_None) desktopTab_ = vr::VROverlay()->IsOverlayVisible(h);
    }
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

// ---------------------------------------------------------------- placement

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

// Mica: a look at the scene around the notification, blended into its
// colors so they change smoothly.
void Presenter::updateMica(Entry& e, const Mat34& pose, const Mat34& head, double now) {
    if (!settings_.mica || now < e.micaDue) return;
    e.micaDue = now + kMicaInterval;
    const Vec3 c = position(pose), right = axis(pose, 0), up = axis(pose, 1);
    const float hw = settings_.width * 0.5f, hh = e.heightM * 0.5f;
    const Vec3 corners[4] = {c - right * hw + up * hh, c + right * hw + up * hh, c + right * hw - up * hh, c - right * hw - up * hh};
    MicaField f;
    if (!sampler_.sample(corners, head, f)) return;
    if (!e.field.valid() || e.field.rgb.size() != f.rgb.size()) {
        e.field = std::move(f);
        renderBackdrop(e);
        return;
    }
    float change = 0;
    for (size_t i = 0; i < f.rgb.size(); ++i) {
        float v = e.field.rgb[i] + (f.rgb[i] - e.field.rgb[i]) * kMicaBlend;
        change = std::max(change, std::fabs(v - e.field.rgb[i]));
        e.field.rgb[i] = v;
    }
    e.field.px0 = f.px0; e.field.px1 = f.px1; e.field.py0 = f.py0; e.field.py1 = f.py1;
    if (change > 1.5f / 255.0f) renderBackdrop(e);
}

void Presenter::place(double now, float dt) {
    const Mode m = !dashboard_ ? Mode::Follow : desktopTab_ ? Mode::Hidden : Mode::Dashboard;
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
        Vec3 right = axis(corner, 0), up = axis(corner, 1), normal = axis(corner, 2);
        if ((eye - cornerPos).dot(normal) < 0) { normal = normal * -1; right = right * -1; } // face the viewer
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
            barX_ = right;
            barY_ = up;
            barZ_ = normal;
            haveBar_ = true;
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
        dockBlend_ = docked ? 1.0f : 0.0f;
        dockBlendVel_ = 0;
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
        dockBlend_ = smoothDamp(dockBlend_, docked ? 1.0f : 0.0f, dockBlendVel_, settings_.followTime, dt);
    }

    // Orientation: facing you while following; docked, the menu's own
    // orientation (no turning with your head). Blended while switching.
    const float k = haveBar_ ? std::clamp(dockBlend_, 0.0f, 1.0f) : 0.0f;
    Vec3 z = (eye - pos_).normalized(), upRef{0, 1, 0};
    if (k > 0) {
        z = (z * (1 - k) + barZ_ * k).normalized();
        upRef = (upRef * (1 - k) + barY_ * k).normalized();
    }
    Vec3 x = upRef.cross(z);
    x = x.length() < 1e-3f ? Vec3{1, 0, 0} : x.normalized();
    const Mat34 upright = basis(x, z.cross(x), z, pos_);
    // Docked, the stack leans back towards you around the newest one's bottom
    // edge, so tall notifications clear the curved menu window.
    if (newest) anchorH_ = newest->heightM;
    const Vec3 pivot = pos_ - axis(upright, 1) * (anchorH_ * 0.5f);
    const Mat34 group = upright * Mat34::rotationX(rad(kDockTilt) * k);
    const Vec3 up = axis(group, 1);

    // Footers: revealed while a click on their notification is recent.
    bool animating = false;
    for (auto& e : entries_) {
        float want = e->state == State::Showing && now < e->footUntil ? 1.0f : 0.0f;
        e->footT += std::clamp(want - e->footT, -dt / kFooterTime, dt / kFooterTime);
        animating |= e->footT != want;
    }
    auto footExtent = [&](const Entry& e) { return easeInOutCubic(e.footT) * (kFooterGap + footerHm_); };

    // Stack: newest at the bottom (on the anchor), older ones above, each
    // above the footer of the one below it.
    std::vector<Entry*> stack;
    for (auto& e : entries_)
        if (e->state == State::Showing) stack.push_back(e.get());
    std::sort(stack.begin(), stack.end(), [](Entry* a, Entry* b) { return a->shownAt > b->shownAt; });
    float bottom = 0; // bottom edge of the next one up
    bool moving = false;
    for (size_t i = 0; i < stack.size(); ++i) {
        Entry& e = *stack[i];
        float want = i == 0 ? 0.0f : bottom + footExtent(e) + e.heightM * 0.5f;
        bottom = want + e.heightM * 0.5f + kStackGap;
        if (!e.yPlaced) { e.y = want; e.yPlaced = true; }
        e.y = smoothDamp(e.y, want, e.yVel, kStackTime, dt);
        moving |= std::fabs(e.y - want) > 1e-4f;
    }
    auto center = [&](const Entry& e) { return pivot + up * (anchorH_ * 0.5f + e.y); };

    // A controller pointing at a notification and clicking: shows how to act
    // on it (the Desktop view), and keeps it up meanwhile, as hovering does on
    // Windows.
    if (m != Mode::Hidden && input_.ok()) {
        input_.update();
        const Vec3 right = axis(group, 0), normal = axis(group, 2);
        for (int hand = 0; hand < 2; ++hand) {
            float o[3], d[3];
            if (!input_.clicked(hand) || !input_.pointerRay(hand, o, d)) continue;
            const Vec3 origin{o[0], o[1], o[2]}, dir{d[0], d[1], d[2]};
            const float dn = dir.dot(normal);
            if (dn > -1e-4f) continue; // parallel, or from behind
            for (Entry* e : stack) {
                const Vec3 c = center(*e);
                const float t = (c - origin).dot(normal) / dn;
                if (t <= 0 || t > 4.0f) continue;
                const Vec3 hit = origin + dir * t - c;
                const float lx = hit.dot(right), ly = hit.dot(up), hh = e->heightM * 0.5f;
                if (std::fabs(lx) > settings_.width * 0.5f || ly > hh || ly < -hh - footExtent(*e)) continue;
                if (e->footCanvas.width() == 0) {
                    const MicaField* field = settings_.mica && e->field.valid() ? &e->field : nullptr;
                    float h = renderer_.renderFooter(footerText(), field, e->theme, settings_.renderScale, kFallbackOpacity, e->footCanvas);
                    footerHm_ = h * settings_.width / ToastRenderer::kWidth;
                    e->footer = std::make_unique<ToastOverlay>();
                    if (e->footer->create("swn.toast." + std::to_string(++overlayCounter_) + ".footer", 198)) e->footer->upload(e->footCanvas);
                    else e->footer.reset();
                }
                e->footUntil = now + kFooterShown;
                e->expiresAt = std::max(e->expiresAt, now + kFooterShown);
                LOG("toast %lld: clicked", (long long)e->toast.id);
                break;
            }
        }
    }

    // Overlay poses: notifications appear and disappear in place, fading.
    const float widthScale = settings_.width / (ToastRenderer::kWidth * settings_.renderScale); // m per content pixel
    const float ga = easeInOutCubic(std::clamp(groupAlpha_, 0.0f, 1.0f));
    animating |= moving || chasing_ || groupAlpha_ != wantAlpha || std::fabs(dockBlend_ - (docked ? 1.0f : 0.0f)) > 1e-3f;
    for (auto& e : entries_) {
        if (!e->overlay || (e->state != State::Showing && e->state != State::Leaving)) continue;
        float alpha = easeInOutCubic(std::clamp(float((now - e->shownAt) / kEnterTime), 0.0f, 1.0f));
        if (e->state == State::Leaving) alpha *= 1 - easeInOutCubic(std::clamp(float((now - e->leaveAt) / kLeaveTime), 0.0f, 1.0f));
        animating |= now - e->shownAt < kEnterTime || e->state == State::Leaving;
        const Vec3 p = center(*e);
        Mat34 pose = group;
        pose.m[0][3] = p.x; pose.m[1][3] = p.y; pose.m[2][3] = p.z;
        if (alpha * ga > 0.004f) updateMica(*e, pose, head, now);
        const float widthM = float(e->canvas.width()) * widthScale;
        e->overlay->place(pose, widthM, alpha * ga);
        if (e->backdrop) e->backdrop->place(pose, widthM, alpha * ga);
        if (e->footer) {
            // Slides down out of the notification's bottom edge (it starts
            // tucked behind it: drawn underneath).
            const float k = easeInOutCubic(e->footT), hh = e->heightM * 0.5f;
            const float tucked = hh - footerHm_ * 0.5f, out = hh + kFooterGap + footerHm_ * 0.5f;
            const Vec3 fp = p - up * (tucked + (out - tucked) * k);
            Mat34 fpose = group;
            fpose.m[0][3] = fp.x; fpose.m[1][3] = fp.y; fpose.m[2][3] = fp.z;
            e->footer->place(fpose, float(e->footCanvas.width()) * widthScale, alpha * ga * std::min(1.0f, k * 2.0f));
        }
    }
    pace_ = animating ? Pace::Frame : Pace::Watch;
}

void Presenter::tick(double now) {
    float dt = lastTick_ > 0 ? float(std::clamp(now - lastTick_, 0.0, 0.1)) : 0.0f;
    lastTick_ = now;
    pollDesktopTab(now);
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
            sampler_.release();
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

// English first; French when chosen, or when Windows is in French.
std::string Presenter::footerText() const {
    bool french = settings_.language == "fr" ||
                  (settings_.language == "auto" && PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH);
    return french ? "Ouvrez la Vue Bureau pour interagir avec la notification"
                  : "Open the Desktop view to interact with the notification";
}
