#include "notify/toast.h"

#include <windows.h>

#include <cstdlib>
#include <cstring>

#include "core/util.h"
#include "notify/xml.h"

namespace {

bool ieq(const std::string& a, const char* b) { return _stricmp(a.c_str(), b) == 0; }

// "{progressValue}" -> data["progressValue"]; anything else is kept as is.
std::string bind(const std::string& s, const std::map<std::string, std::string>& data) {
    if (s.find('{') == std::string::npos) return s;
    std::string out;
    for (size_t i = 0; i < s.size();) {
        size_t b = s.find('{', i);
        if (b == std::string::npos) { out.append(s, i, std::string::npos); break; }
        size_t e = s.find('}', b);
        if (e == std::string::npos) { out.append(s, i, std::string::npos); break; }
        out.append(s, i, b - i);
        auto it = data.find(s.substr(b + 1, e - b - 1));
        out += it != data.end() ? it->second : s.substr(b, e - b + 1);
        i = e + 1;
    }
    return out;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

bool frenchUi() { return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH; }

struct Builder {
    const std::map<std::string, std::string>& data;
    Toast& t;
    bool legacy = false;

    void text(const xml::Node& n) {
        std::string s = trim(bind(n.innerText(), data));
        if (s.empty()) return;
        if (ieq(n.attr("placement"), "attribution")) { t.attribution = s; return; }
        if (t.title.empty()) t.title = s;
        else t.lines.push_back(s);
    }

    void image(const xml::Node& n) {
        ToastImage img;
        img.src = trim(bind(n.attr("src"), data));
        img.circle = ieq(n.attr("hint-crop"), "circle");
        if (img.src.empty()) return;
        const std::string& placement = n.attr("placement");
        if (ieq(placement, "appLogoOverride") || (legacy && t.logo.empty())) t.logo = img;
        else if (ieq(placement, "hero")) t.hero = img;
        else if (t.inlineImage.empty()) t.inlineImage = img;
    }

    void progress(const xml::Node& n) {
        ToastProgress p;
        p.title = trim(bind(n.attr("title"), data));
        p.status = trim(bind(n.attr("status"), data));
        p.valueString = trim(bind(n.attr("valueStringOverride"), data));
        std::string v = trim(bind(n.attr("value"), data));
        p.value = ieq(v, "indeterminate") ? -1.0 : std::strtod(v.c_str(), nullptr);
        if (p.value > 1.0) p.value = 1.0;
        t.progress.push_back(p);
    }

    // Binding content; groups / subgroups are flattened.
    void content(const xml::Node& n, int depth) {
        if (depth > 8) return;
        for (auto& c : n.children) {
            if (c->isText()) continue;
            if (ieq(c->name, "text")) text(*c);
            else if (ieq(c->name, "image")) image(*c);
            else if (ieq(c->name, "progress")) progress(*c);
            else if (ieq(c->name, "group") || ieq(c->name, "subgroup")) content(*c, depth + 1);
        }
    }

    void actions(const xml::Node& n) {
        for (auto& c : n.children) {
            if (c->isText()) continue;
            if (ieq(c->name, "action")) {
                t.hasActions = true;
                if (ieq(c->attr("placement"), "contextMenu")) continue;
                std::string label = trim(bind(c->attr("content"), data));
                if (label.empty() && ieq(c->attr("activationType"), "system")) {
                    // System buttons without a label: Windows shows its own.
                    const std::string& a = c->attr("arguments");
                    if (ieq(a, "dismiss")) label = frenchUi() ? "Ignorer" : "Dismiss";
                    else if (ieq(a, "snooze")) label = frenchUi() ? "R\xC3\xA9p\xC3\xA9ter" : "Snooze";
                }
                if (!label.empty()) t.buttons.push_back(label);
            } else if (ieq(c->name, "input")) {
                std::string label = trim(bind(c->attr("placeHolderContent"), data));
                if (label.empty()) label = trim(bind(c->attr("title"), data));
                if (ieq(c->attr("type"), "selection")) {
                    // Shown with the default selection, like Windows' combo box.
                    const std::string& def = c->attr("defaultInput");
                    for (auto& s : c->children)
                        if (!s->isText() && ieq(s->name, "selection") && (def.empty() || s->attr("id") == def)) {
                            label = trim(bind(s->attr("content"), data));
                            break;
                        }
                }
                t.inputs.push_back(label);
            }
        }
    }
};

} // namespace

bool Toast::sameContent(const Toast& o) const {
    auto img = [](const ToastImage& a, const ToastImage& b) { return a.src == b.src && a.circle == b.circle; };
    if (title != o.title || lines != o.lines || attribution != o.attribution || buttons != o.buttons || inputs != o.inputs ||
        !img(logo, o.logo) || !img(hero, o.hero) || !img(inlineImage, o.inlineImage) || progress.size() != o.progress.size())
        return false;
    for (size_t i = 0; i < progress.size(); ++i) {
        const ToastProgress &a = progress[i], &b = o.progress[i];
        if (a.title != b.title || a.status != b.status || a.valueString != b.valueString || a.value != b.value) return false;
    }
    return true;
}

bool parseToast(const std::string& payload, const std::map<std::string, std::string>& data, Toast& out) {
    std::string text = payload;
    // Some writers store UTF-16.
    if (text.size() >= 2 && (unsigned char)text[0] == 0xFF && (unsigned char)text[1] == 0xFE)
        text = util::toUtf8(std::wstring((const wchar_t*)(text.data() + 2), (text.size() - 2) / 2));
    auto root = xml::parse(text);
    if (!root || !ieq(root->name, "toast")) return false;

    const std::string& sc = root->attr("scenario");
    out.scenario = ieq(sc, "reminder") ? Scenario::Reminder
                 : ieq(sc, "alarm") ? Scenario::Alarm
                 : ieq(sc, "incomingCall") ? Scenario::IncomingCall
                 : ieq(sc, "urgent") ? Scenario::Urgent
                 : Scenario::Default;
    out.longDuration = ieq(root->attr("duration"), "long");

    Builder b{data, out};
    if (const xml::Node* visual = root->child("visual")) {
        const xml::Node* binding = nullptr;
        for (auto& c : visual->children)
            if (!c->isText() && ieq(c->name, "binding")) {
                if (!binding) binding = c.get();
                if (ieq(c->attr("template"), "ToastGeneric")) { binding = c.get(); break; }
            }
        if (binding) {
            // Legacy templates (ToastImageAndText0x): the image is the logo.
            b.legacy = !ieq(binding->attr("template"), "ToastGeneric");
            b.content(*binding, 0);
        }
    }
    if (const xml::Node* actions = root->child("actions")) b.actions(*actions);
    return true;
}
