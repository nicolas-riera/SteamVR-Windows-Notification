#pragma once
// Read-only access to Windows' notification database
// (%LOCALAPPDATA%\Microsoft\Windows\Notifications\wpndatabase.db), through the
// SQLite that ships with Windows. Every toast an app shows lands there with its
// XML payload, and is deleted when the user dismisses it.
#include <windows.h>

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

struct sqlite3;
struct sqlite3_stmt;

struct ToastRow {
    int64_t id = 0, order = 0;
    std::string aumid, payload, tag, group;
};

class WpnStore {
public:
    ~WpnStore();
    bool open();
    bool isOpen() const { return db_ != nullptr; }

    // Directory change notification, signalled when the database files are
    // written (may lag: the caller also checks changed() on a timer).
    HANDLE changeHandle() const { return change_; }
    void rearm();

    // Cheap: true if another connection committed since the last call.
    bool changed();

    // Toasts inserted or replaced since the previous call. The first call
    // only records the toasts already there (they are never shown).
    std::vector<ToastRow> fetchNew();
    // Ids of the toasts present at the last fetchNew().
    std::set<int64_t> toastIds();
    std::map<std::string, std::string> data(int64_t id);

    // Per-app notification settings ("Show notification banners").
    bool bannerAllowed(const std::string& aumid);
    std::string asset(const std::string& aumid, const char* key); // DisplayName, IconUri

private:
    sqlite3_stmt* prepare(const char* sql);
    void close();

    sqlite3* db_ = nullptr;
    HANDLE change_ = INVALID_HANDLE_VALUE;
    std::set<std::pair<int64_t, int64_t>> known_; // (Order, Id) of the toasts at the last scan
    bool scanned_ = false;
    long long dataVersion_ = -1;
};
