#pragma once
// Mirrors Windows' notification banners in VR.
//  - Same rules as Windows: one normal banner at a time (others wait), up to
//    three priority ones (reminders, alarms, calls) on top; same durations;
//    Do Not Disturb and per-app "banners" settings; a toast removed from
//    Windows (dismissed) disappears at once.
//  - Tag-along placement, like Windows Mixed Reality: the notifications float
//    below the gaze, stay still while within a small cone, then glide back.
//  - SteamVR menu: opening its Desktop tab counts as going to see the
//    notification (the real banner is there): it is closed in VR. Otherwise
//    docked above the right end of the menu bar, with the menu's orientation
//    (and back in front of you when you look away from the menu).
//  - Mica background made from the VR scene behind (see MirrorSampler).
#include <windows.h>
#include <openvr.h>

#include <memory>
#include <string>
#include <vector>

#include "core/mathx.h"
#include "gfx/canvas.h"
#include "notify/app_info.h"
#include "notify/downloader.h"
#include "render/toast_renderer.h"
#include "vr/mirror_sampler.h"
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
        std::unique_ptr<ToastOverlay> overlay;  // content
        std::unique_ptr<ToastOverlay> backdrop; // Mica + shadow, underneath
        Canvas canvas, backCanvas;
        Theme theme;
        MicaField field;
        double micaDue = 0;
        float panelH = 0;  // panel height (layout units)
        float heightM = 0; // panel height (m)
        float y = 0, yVel = 0; // stack offset (m)
        bool yPlaced = false;
    };
    enum class Mode { Follow, Dashboard, Hidden };

    bool allowed(const Toast& t);
    double duration(const Toast& t) const;
    void loadImages(Entry& e);
    bool loadImage(const ToastImage& img, const std::string& aumid, int maxW, int maxH, Image& out);
    void show(Entry& e, double now);
    void leave(Entry& e, double now);
    void render(Entry& e);
    void renderBackdrop(Entry& e);
    void lifecycle(double now);
    void pollDesktopTab(double now);
    bool headPose(Mat34& head);
    bool barTopRight(double now, Mat34& corner);
    void updateMica(Entry& e, const Mat34& pose, const Mat34& head, double now);
    void place(double now, float dt);

    const Settings& settings_;
    FontSet& fonts_;
    WpnStore& store_;
    AppDirectory apps_;
    Downloader downloader_;
    ToastRenderer renderer_;
    MirrorSampler sampler_;
    std::vector<std::unique_ptr<Entry>> entries_;
    int overlayCounter_ = 0;
    Pace pace_ = Pace::Idle;
    double lastTick_ = 0;
    double idleSince_ = -1;
    bool trimmed_ = false;

    // Group placement (world, standing universe).
    bool placed_ = false;
    bool fresh_ = true; // nothing was on screen
    Vec3 pos_;
    float yawVel_ = 0, pitchVel_ = 0, distVel_ = 0;
    bool chasing_ = false;
    float lastYaw_ = 0;
    float groupAlpha_ = 0; // 0..1, linear (eased when applied)

    // SteamVR menu.
    bool dashboard_ = false;
    bool desktopTab_ = false;
    double nextTabPoll_ = 0;
    bool docked_ = true; // at the bar, not detached by looking away
    double nextBarScan_ = 0;
    float barRight_ = 0; // right end of the bar content (overlay pixels)
    bool haveBar_ = false;
    Vec3 barX_, barY_, barZ_; // orientation of the bar (docked notifications use it)
    float dockBlend_ = 0, dockBlendVel_ = 0;
};
