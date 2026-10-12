#pragma once
// What a toast shows, extracted from its XML payload (adaptive toast schema).
#include <cstdint>
#include <map>
#include <string>
#include <vector>

enum class Scenario { Default, Reminder, Alarm, IncomingCall, Urgent };

struct ToastImage {
    std::string src; // as written in the payload (file:///, ms-appx:///, http(s)://, path...)
    bool circle = false;
    bool empty() const { return src.empty(); }
};

struct ToastProgress {
    std::string title, status, valueString;
    double value = 0; // 0..1; < 0 = indeterminate
};

struct Toast {
    // Database identity.
    int64_t id = 0;
    int64_t order = 0; // grows on every insert or replacement
    std::string aumid; // application user model id (handler)
    std::string tag, group;
    int64_t expiry = 0; // FILETIME; Windows deletes the toast then, but keeps its banner

    // Content.
    Scenario scenario = Scenario::Default;
    bool longDuration = false;
    std::string title;              // first text
    std::vector<std::string> lines; // other texts (body)
    std::string attribution;
    ToastImage logo;                // appLogoOverride ("photo")
    ToastImage hero;
    ToastImage inlineImage;
    std::vector<ToastProgress> progress;
    std::vector<std::string> buttons;    // visible action buttons (contextMenu ones are skipped)
    std::vector<std::string> inputs;     // text box placeholders / selection labels
    bool hasActions = false;             // any button, incl. system ones (persisting scenarios need one)

    // True when the visible content differs (to skip useless redraws).
    bool sameContent(const Toast& o) const;
};

// Parses a toast payload, resolving {bindings} from the notification's data
// (progress bars updated by the app). Returns false if it is not a toast.
bool parseToast(const std::string& payload, const std::map<std::string, std::string>& data, Toast& out);
