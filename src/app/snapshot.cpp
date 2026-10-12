// Offline rendering of sample notifications, for checking the design without
// a headset:  SteamVrWindowsNotification.exe --snapshot <dir>
#include <windows.h>
#include <objbase.h>

#include <cmath>

#include "app/app.h"
#include "core/util.h"
#include "gfx/font.h"
#include "notify/system_state.h"
#include "render/mica.h"
#include "render/toast_renderer.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

namespace {

bool writePng(const Canvas& c, const std::wstring& path) {
    // stb uses narrow paths; write to memory then save with a wide path.
    std::string data;
    auto sink = [](void* ctx, void* d, int n) { static_cast<std::string*>(ctx)->append((const char*)d, size_t(n)); };
    if (!stbi_write_png_to_func(sink, &data, c.width(), c.height(), 4, c.data(), c.width() * 4)) return false;
    return util::writeFile(path, data);
}

Image toImage(const Canvas& c) {
    Image img;
    img.w = c.width();
    img.h = c.height();
    img.px.assign(c.data(), c.data() + size_t(img.w) * img.h * 4);
    return img;
}

// Stand-ins for app icons and photos.
Image appIcon(FontSet& f, Color bg, uint32_t glyph, int px) {
    Canvas c;
    c.resize(px, px);
    c.fillRoundRect({0, 0, float(px), float(px)}, px * 0.22f, bg);
    f.drawIcon(c, px * 0.5f, px * 0.5f, glyph, px * 0.62f, rgb(255, 255, 255));
    return toImage(c);
}

Image avatar(int px) {
    Canvas c;
    c.resize(px, px);
    for (int y = 0; y < px; ++y)
        for (int x = 0; x < px; ++x) {
            float t = float(x + y) / float(2 * px);
            c.blend(x, y, mix(rgb(255, 140, 90), rgb(160, 70, 220), t), 1.0f);
        }
    c.fillCircle(px * 0.5f, px * 0.40f, px * 0.18f, rgb(255, 255, 255, 230));
    c.fillRoundRect({px * 0.22f, px * 0.64f, px * 0.56f, px * 0.5f}, px * 0.25f, rgb(255, 255, 255, 230));
    return toImage(c);
}

void renderIcon(FontSet& f, const std::wstring& path) {
    const int S = 256;
    Canvas c;
    c.resize(S, S);
    c.fillRoundRect({8, 8, S - 16.0f, S - 16.0f}, 52, rgb(32, 32, 32));
    c.strokeRoundRect({8, 8, S - 16.0f, S - 16.0f}, 52, 3, rgb(255, 255, 255, 30));
    // A small notification card.
    Rect card{40, 74, 176, 108};
    c.fillRoundRect(card, 16, rgb(58, 58, 58));
    c.fillCircle(card.x + 36, card.cy(), 20, rgb(76, 194, 255));
    c.fillRoundRect({card.x + 70, card.y + 32, 86, 14}, 7, rgb(255, 255, 255, 235));
    c.fillRoundRect({card.x + 70, card.y + 60, 64, 12}, 6, rgb(255, 255, 255, 140));
    c.fillCircle(card.right() - 6, card.y + 6, 15, rgb(255, 92, 92));
    writePng(c, path);
}

} // namespace

int runSnapshot(const std::wstring& outDir) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CreateDirectoryW(outDir.c_str(), nullptr);
    FontSet fonts;
    if (!fonts.init()) return 1;
    Mica mica;
    ToastRenderer renderer(fonts, mica);
    const float s = 2.0f;
    const int icon = ToastRenderer::iconPx(s);

    std::vector<std::pair<std::string, ToastVisual>> samples;
    {
        ToastVisual v;
        v.appName = "Discord";
        v.appIcon = appIcon(fonts, rgb(88, 101, 242), 0xE8BD, icon);
        v.toast.title = "Lucas";
        v.toast.lines = {"t'es dispo pour une session VRChat ce soir ? On se retrouve dans le monde d'hier vers 21 h"};
        v.toast.logo = {"x", true};
        v.logo = avatar(ToastRenderer::logoPx(s));
        samples.push_back({"message", v});
    }
    {
        ToastVisual v;
        v.appName = "Outil Capture d’écran";
        v.appIcon = appIcon(fonts, rgb(0, 120, 212), 0xE722, icon);
        v.toast.title = "Capture d’écran copiée dans le Presse-papiers";
        v.toast.lines = {"Enregistrement automatique dans le dossier des captures d’écran."};
        v.toast.buttons = {"Balisage et partage"};
        loadImage(sys::wallpaperPath(), ToastRenderer::heroMaxPx(s), ToastRenderer::heroMaxPx(s), v.hero);
        v.toast.hero.src = "x";
        samples.push_back({"hero", v});
    }
    {
        ToastVisual v;
        v.appName = "Steam";
        v.appIcon = appIcon(fonts, rgb(23, 26, 33), 0xE896, icon);
        v.toast.title = "Téléchargement en cours";
        v.toast.lines = {"SteamVR Beta"};
        v.toast.progress = {{"Mise à jour", "Téléchargement…", "1,2 Go / 1,9 Go", 0.62}};
        samples.push_back({"progress", v});
    }
    {
        ToastVisual v;
        v.appName = "Calendrier";
        v.appIcon = appIcon(fonts, rgb(16, 124, 16), 0xE787, icon);
        v.toast.scenario = Scenario::Reminder;
        v.toast.title = "Réunion d’équipe";
        v.toast.lines = {"Dans 5 minutes", "Salle Teams"};
        v.toast.inputs = {"Rappeler dans 5 minutes"};
        v.toast.buttons = {"Répéter", "Ignorer"};
        samples.push_back({"reminder", v});
    }
    {
        ToastVisual v;
        v.appName = "Sécurité Windows";
        v.appIcon = appIcon(fonts, rgb(0, 95, 184), 0xE83D, icon);
        v.toast.title = "Résumé de Microsoft Defender";
        v.toast.lines = {"L’Antivirus Microsoft Defender a réussi à effectuer une action sur 1 menaces depuis le dernier "
                         "récapitulatif. Aucune action supplémentaire n’est nécessaire de votre part pour le moment."};
        samples.push_back({"long", v});
    }

    uint32_t accent = sys::accentColor();
    for (auto& [name, v] : samples)
        for (bool light : {false, true}) {
            Canvas c;
            renderer.render(v, Theme::make(light, accent), s, true, c);
            writePng(c, outDir + L"\\" + util::fromUtf8(name) + (light ? L"_light.png" : L"_dark.png"));
        }
    renderIcon(fonts, outDir + L"\\icon.png");
    CoUninitialize();
    return 0;
}
