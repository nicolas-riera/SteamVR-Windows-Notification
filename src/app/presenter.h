#pragma once
// Mirrors Windows' notification banners in VR.
//  - Same rules as Windows: one normal banner at a time (others wait), up to
//    three priority ones (reminders, alarms, calls) on top; same durations;
//    Do Not Disturb and per-app "banners" settings; a toast removed from
//    Windows (dismissed) disappears at once.
//  - Tag-along placement, like Windows Mixed Reality: the notifications float
//    below the gaze, stay still while within a small cone, then glide back.
//  - SteamVR menu: hidden while the Desktop tab is shown (the real banner is
//    visible there), otherwise moved in front of the menu.
#include <windows.h>
#include <openvr.h>

#include <memory>
#include <string>
#include <vector>

#include "core/mathx.h"
#include "gfx/canvas.h"
#include "notify/app_info.h"
#include "notify/downloader.h"
#include "render/mica.h"
#include "render/toast_renderer.h"
#include "vr/toast_overlay.h"

struct Settings;
class FontSet;
class WpnStore;

class Presenter {
public:
    Presenter(const Settings& settings, FontSet& fonts, WpnStore& store);
    ~Presenter();

    // After the notification database changed.
    void syncDatabase(double now);
    // After a remote image finished downloading.
    void imagesArrived(double now);
    void setDashboard(bool open);
    void tick(double now);

    // How soon the main loop should call tick() again.
    enum class Pace { Idle, Watch, Frame };
    Pace pace() const { return pace_; }
    HANDLE downloadEvent() const { return downloader_.event(); }

private:
    enum class State { Waiting, Queued, Showing, Leaving };
    struct Entry {
        Toast toast;
        std::string payload;
        ToastVisual visual;
        State state = State::Waiting;
        bool priority = false;
        bool imagesPending = false;
        bool dirty = true; // needs a redraw
        double arrived = 0, shownAt = 0, expiresAt = 0, leaveAt = 0;
        std::unique_ptr<ToastOverlay> overlay;
        Canvas canvas;
        float heightM = 0; // panel height (m)
        float y = 0, yVel = 0;  // stack offset (m)
        bool yPlaced = false;
    };
    enum class Mode { Follow, Dashboard, Hidden };

    bool allowed(const Toast& t);
    double duration(const Toast& t) const;
    void loadImages(Entry& e);
    bool loadImage(const ToastImage& img, const std::string& aumid, int maxW, int maxH, Image& out);
    void show(Entry& e, double now);
    void render(Entry& e);
    void lifecycle(double now);
    Mode mode(double now);
    bool headPose(Mat34& head);
    bool dashboardAnchor(const Vec3& head, Vec3& target);
    void place(double now, float dt);

    const Settings& settings_;
    FontSet& fonts_;
    WpnStore& store_;
    AppDirectory apps_;
    Downloader downloader_;
    Mica mica_;
    ToastRenderer renderer_;
    std::vector<std::unique_ptr<Entry>> entries_;
    int overlayCounter_ = 0;
    Pace pace_ = Pace::Idle;
    double lastTick_ = 0;

    // Group placement (world, standing universe).
    bool placed_ = false;
    bool fresh_ = true; // nothing was on screen
    Vec3 pos_;
    float yawVel_ = 0, pitchVel_ = 0, distVel_ = 0;
    bool chasing_ = false;
    float lastYaw_ = 0;
    float groupAlpha_ = 0;     // 0..1, linear (eased when applied)
    Mode lastMode_ = Mode::Follow;

    // SteamVR menu.
    bool dashboard_ = false;
    bool desktopTab_ = false;
    double nextTabPoll_ = 0;
    bool dashTargetSet_ = false;
    Vec3 dashTarget_;
    Vec3 dashCenter_;
};
