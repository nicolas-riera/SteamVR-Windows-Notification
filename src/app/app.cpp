#include "app/app.h"

#include <windows.h>
#include <objbase.h>
#include <tlhelp32.h>
#include <openvr.h>

#include "app/presenter.h"
#include "core/log.h"
#include "core/settings.h"
#include "core/util.h"
#include "gfx/font.h"
#include "notify/wpn_store.h"

namespace {

bool processRunning(const wchar_t* exe) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{sizeof(pe)};
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
        found = _wcsicmp(pe.szExeFile, exe) == 0;
    CloseHandle(snap);
    return found;
}

// Registers the manifest so SteamVR starts (and stops) us with itself.
void registerApp(const std::wstring& dir) {
    vr::IVRApplications* apps = vr::VRApplications();
    std::string manifest = util::toUtf8(dir + L"\\manifest.vrmanifest");
    bool installed = apps->IsApplicationInstalled(kAppKey);
    bool autoLaunch = true; // first run: on
    if (installed) {
        // Folder moved or renamed: SteamVR still points at the old copy. Drop
        // that registration (keeping the user's autostart choice) and re-add.
        char bin[1024] = {};
        vr::EVRApplicationError pe = vr::VRApplicationError_None;
        apps->GetApplicationPropertyString(kAppKey, vr::VRApplicationProperty_BinaryPath_String, bin, sizeof(bin), &pe);
        wchar_t self[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        std::wstring old = util::fromUtf8(bin);
        if (pe == vr::VRApplicationError_None && !old.empty() && _wcsicmp(old.c_str(), self) != 0) {
            autoLaunch = apps->GetApplicationAutoLaunch(kAppKey);
            std::wstring oldDir = old.substr(0, old.find_last_of(L"\\/"));
            apps->RemoveApplicationManifest(util::toUtf8(oldDir + L"\\manifest.vrmanifest").c_str());
            installed = apps->IsApplicationInstalled(kAppKey);
            LOG("vr: moved from %s, old registration %s", bin, installed ? "NOT removed" : "removed");
        }
    }
    vr::EVRApplicationError e = apps->AddApplicationManifest(manifest.c_str(), false);
    if (e != vr::VRApplicationError_None)
        LOG("vr: AddApplicationManifest: %s", apps->GetApplicationsErrorNameFromEnum(e));
    if (!installed && e == vr::VRApplicationError_None) {
        apps->SetApplicationAutoLaunch(kAppKey, autoLaunch); // otherwise respect the user's choice
        LOG("vr: registered, auto-launch %s", autoLaunch ? "on" : "off");
    }
    apps->IdentifyApplication(GetCurrentProcessId(), kAppKey);
}

} // namespace

int runOverlay() {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\SteamVrWindowsNotification");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    const std::wstring dir = util::exeDir();
    logx::init(dir + L"\\notification.log");

    // Never start SteamVR ourselves: only run alongside it.
    if (!processRunning(L"vrserver.exe")) {
        LOG("SteamVR is not running, exiting");
        return 0;
    }
    vr::EVRInitError initErr = vr::VRInitError_None;
    vr::VR_Init(&initErr, vr::VRApplication_Overlay);
    if (initErr != vr::VRInitError_None) {
        LOG("VR_Init failed: %s", vr::VR_GetVRInitErrorAsEnglishDescription(initErr));
        return 1;
    }
    registerApp(dir);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    Settings settings;
    const std::wstring settingsPath = dir + L"\\settings.json";
    settings.load(settingsPath);
    settings.save(settingsPath); // hand-editable, with every key (new ones included)

    FontSet fonts;
    if (!fonts.init()) {
        vr::VR_Shutdown();
        return 1;
    }
    WpnStore store;
    store.open();
    Presenter presenter(settings, fonts, store);
    LOG("running");

    double nextDbCheck = 0, nextOpenTry = util::now() + 5.0;
    bool running = true;
    while (running) {
        // Sleep as long as possible: every frame only while something moves,
        // ~30 Hz while a notification is shown (to notice head turns), and
        // 4 Hz with nothing on screen (database / SteamVR events).
        bool dbSignalled = false;
        if (presenter.pace() == Presenter::Pace::Frame) {
            vr::VROverlay()->WaitFrameSync(30);
            if (WaitForSingleObject(presenter.downloadEvent(), 0) == WAIT_OBJECT_0) presenter.imagesArrived(util::now());
        } else {
            HANDLE handles[2];
            DWORD n = 0;
            handles[n++] = presenter.downloadEvent();
            if (store.changeHandle() != INVALID_HANDLE_VALUE) handles[n++] = store.changeHandle();
            DWORD r = WaitForMultipleObjects(n, handles, FALSE, presenter.pace() == Presenter::Pace::Watch ? 33 : 250);
            if (r == WAIT_OBJECT_0) presenter.imagesArrived(util::now());
            if (r == WAIT_OBJECT_0 + 1) {
                store.rearm();
                dbSignalled = true;
            }
        }
        double now = util::now();

        vr::VREvent_t e;
        while (vr::VRSystem()->PollNextEvent(&e, sizeof(e))) {
            switch (e.eventType) {
            case vr::VREvent_Quit:
                vr::VRSystem()->AcknowledgeQuit_Exiting();
                running = false;
                break;
            case vr::VREvent_DashboardActivated: presenter.setDashboard(true); break;
            case vr::VREvent_DashboardDeactivated: presenter.setDashboard(false); break;
            default: break;
            }
        }

        if (store.isOpen()) {
            if (dbSignalled || now >= nextDbCheck) {
                nextDbCheck = now + 0.1;
                if (store.changed()) presenter.syncDatabase(now);
            }
        } else if (now >= nextOpenTry) {
            nextOpenTry = now + 5.0;
            store.open();
        }
        presenter.tick(now);
    }

    LOG("exiting");
    vr::VR_Shutdown();
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return 0;
}

int runUnregister() {
    vr::EVRInitError initErr = vr::VRInitError_None;
    vr::VR_Init(&initErr, vr::VRApplication_Utility);
    if (initErr != vr::VRInitError_None) return 1;
    std::string manifest = util::toUtf8(util::exeDir() + L"\\manifest.vrmanifest");
    vr::VRApplications()->RemoveApplicationManifest(manifest.c_str());
    vr::VR_Shutdown();
    return 0;
}
