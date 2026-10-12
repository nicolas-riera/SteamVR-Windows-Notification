#include "core/settings.h"

#include <algorithm>

#include "core/json.h"
#include "core/util.h"

bool Settings::load(const std::wstring& path) {
    std::string text;
    if (!util::readFile(path, text)) return false;
    json::Value j;
    if (!json::Value::parse(text, j) || !j.isObject()) return false;

    distance = std::clamp(j["distance"].asFloat(distance), 0.3f, 3.0f);
    width = std::clamp(j["width"].asFloat(width), 0.1f, 1.5f);
    pitch = std::clamp(j["pitch"].asFloat(pitch), -60.0f, 30.0f);
    deadzoneYaw = std::clamp(j["deadzoneYaw"].asFloat(deadzoneYaw), 0.0f, 60.0f);
    deadzonePitch = std::clamp(j["deadzonePitch"].asFloat(deadzonePitch), 0.0f, 60.0f);
    followTime = std::clamp(j["followTime"].asFloat(followTime), 0.02f, 2.0f);
    theme = j["theme"].asString();
    if (theme != "dark" && theme != "light") theme = "auto";
    mica = j["mica"].asBool(mica);
    renderScale = std::clamp(j["renderScale"].asFloat(renderScale), 1.0f, 3.0f);
    respectDoNotDisturb = j["respectDoNotDisturb"].asBool(respectDoNotDisturb);
    shortDuration = std::clamp(j["shortDuration"].asFloat(shortDuration), 0.0f, 120.0f);
    longDuration = std::clamp(j["longDuration"].asFloat(longDuration), 1.0f, 300.0f);
    keepReminders = j["keepReminders"].asBool(keepReminders);
    excludedApps.clear();
    for (const json::Value& v : j["excludedApps"].elements())
        if (v.isString() && !v.asString().empty()) excludedApps.push_back(v.asString());
    return true;
}

bool Settings::save(const std::wstring& path) const {
    // Written by hand (one key per line) so the file stays easy to edit.
    std::string apps;
    for (size_t i = 0; i < excludedApps.size(); ++i) {
        if (i) apps += ", ";
        apps += json::Value(excludedApps[i]).dump();
    }
    std::string s = "{\n";
    s += util::format("  \"distance\": %.2f,\n", distance);
    s += util::format("  \"width\": %.2f,\n", width);
    s += util::format("  \"pitch\": %.1f,\n", pitch);
    s += util::format("  \"deadzoneYaw\": %.1f,\n", deadzoneYaw);
    s += util::format("  \"deadzonePitch\": %.1f,\n", deadzonePitch);
    s += util::format("  \"followTime\": %.2f,\n", followTime);
    s += util::format("  \"theme\": \"%s\",\n", theme.c_str());
    s += util::format("  \"mica\": %s,\n", mica ? "true" : "false");
    s += util::format("  \"renderScale\": %.1f,\n", renderScale);
    s += util::format("  \"respectDoNotDisturb\": %s,\n", respectDoNotDisturb ? "true" : "false");
    s += util::format("  \"shortDuration\": %.1f,\n", shortDuration);
    s += util::format("  \"longDuration\": %.1f,\n", longDuration);
    s += util::format("  \"keepReminders\": %s,\n", keepReminders ? "true" : "false");
    s += "  \"excludedApps\": [" + apps + "]\n}\n";
    return util::writeFile(path, s);
}
