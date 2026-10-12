# SteamVR Windows Notification

Shows Windows notifications in VR, the way Windows Mixed Reality did. Each banner Windows shows on the desktop floats in front of you, slightly below your gaze, with the Windows 11 look: app icon and name, photo, title, text, hero image, progress bars and buttons, on a Mica background.

There is no window, no tray icon and no dashboard tab. The game keeps its controllers: the notifications only notice a click aimed at them.

## Install

1. Build the project (see below). Everything SteamVR needs ends up in `dist/`.
2. Run `dist/SteamVrWindowsNotification.exe` once while SteamVR is running.
   This registers it with SteamVR, which then starts and stops it automatically.
   You can turn this off in SteamVR's startup settings.

The executable exits if SteamVR is not running; it never starts SteamVR itself.
To remove the SteamVR registration, run `SteamVrWindowsNotification.exe --unregister`.

## Behaviour

**Mirrors the desktop banners.** The rules follow what Windows does, as measured on the desktop:

- One normal banner at a time; the others queue. Reminders, alarms and calls can show up to three at once, on top.
- A banner lasts as long as Windows' *Dismiss notifications after* setting, plus about a second. Toasts sent with `duration="long"` last 25 s.
- Do Not Disturb and each app's *Show notification banners* setting are respected.
- A notification removed from Windows (from the notification center, or hidden by its app, as Discord does with its bursts) disappears from VR at once, and the next one takes its place. A notification that merely expired keeps its banner, as on the desktop.
- Progress bars update live.

**Placement.** The notification floats below your gaze and stays still while you look around within a small cone. It then glides back in front of you, easing in and out. It appears and disappears with a fade, in place.

**SteamVR menu.**

- On the *Desktop* tab, you see the real banner, so the notification counts as seen and closes in VR.
- On any other tab, it docks just above the right end of the menu's bottom bar, with the menu's orientation, leaning slightly back so it never cuts into the menu window.
- Looking away from the menu detaches it, and it comes back in front of you.

**Clicking.** Point a controller at a notification and pull the trigger: a strip slides down from under it, saying to open the Desktop view to act on it ("Open the Desktop view to interact with the notification", or in French). The notification stays up a few more seconds, as hovering a banner does on Windows.

**Mica.**

- The background is made like Windows' Mica. The colors behind the notification (here, the VR scene, read from SteamVR's mirror texture) are heavily blurred, given the tint's luminosity, then tinted.
- It shows the colors of what is behind, never its details.
- It follows the Windows light or dark theme.

**Closing a banner with its X.** This leaves no trace other apps can see on current Windows 11 builds (no window, database change or accessibility event). Such a notification therefore stays in VR until its time is up.

Reminders, alarms and calls stay on Windows until you act. In VR they last the long duration, unless `keepReminders` is set.

## Settings

`dist/settings.json` is written on first run with every key, and read at startup.

| Key | Default | Meaning |
|---|---|---|
| `distance` | 0.9 | meters from your eyes |
| `width` | 0.34 | meters, width of a notification |
| `pitch` | -17 | degrees below your gaze |
| `deadzoneYaw` / `deadzonePitch` | 12 / 9 | degrees you can look around before it follows |
| `followTime` | 0.3 | seconds, smoothing of the glide |
| `theme` | `auto` | `auto` (Windows), `dark` or `light` |
| `language` | `auto` | text of the click hint: `auto` (Windows), `en` or `fr` |
| `mica` | true | Mica background; `false` for a plain translucent one |
| `renderScale` | 2 | texture pixels per Windows pixel |
| `respectDoNotDisturb` | true | follow Windows' Do Not Disturb |
| `shortDuration` | 0 | seconds; 0 = Windows' setting + 1 s |
| `longDuration` | 25 | seconds, long toasts, reminders, alarms and calls |
| `keepReminders` | false | keep reminders / alarms / calls until removed from Windows |
| `excludedApps` | `[]` | AppUserModelIDs (or parts of them) to never show |

## Testing

Run `tools/test-notification.cmd`. It opens a small window that sends a notification 3 seconds after you click: a plain one, a message with a photo, a hero image with a button, a live progress bar, a reminder, or a long text.

`SteamVrWindowsNotification.exe --snapshot <dir>` renders sample notifications to PNG files, without a headset.

## Build

You need Visual Studio 2022 (C++) and CMake 3.20 or newer.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The CRT is linked statically, so no redistributable is needed. SQLite is the copy that ships with Windows (`winsqlite3.dll`).

## Architecture

| Folder | Role |
|---|---|
| `src/core` | settings, JSON, log, math (3x4 transforms, easing, spring smoothing) |
| `src/notify` | Windows notification database (`wpndatabase.db`, read-only), toast XML, app names and icons, image URIs, Do Not Disturb, theme |
| `src/gfx` | CPU rasterizer, text (stb_truetype on the memory-mapped system fonts), WIC images |
| `src/render` | Windows 11 toast layout and Mica |
| `src/vr` | overlays, D3D11 upload, mirror texture sampling, SteamVR Input (click) |
| `src/app` | main loop, presenter (queue, timing, placement, SteamVR menu), registration, snapshot |

**Lightweight by design:**

- No polling thread. The loop sleeps on the notification folder's change handle, checks the database version 4 times per second, and polls SteamVR events.
- It wakes every frame only while something moves, and 30 times per second while a notification is shown.
- The content texture is drawn once per notification. The Mica backdrop is a separate low-resolution overlay, refreshed a few times per second from the scene.
- After 30 s without notifications, the scene sampling is released and the memory pages are trimmed. The D3D11 device is kept: SteamVR holds references to it, so a released device would never be freed.
