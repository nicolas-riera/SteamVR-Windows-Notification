#include "notify/wpn_store.h"

#include <shlobj.h>
#include <winsqlite/winsqlite3.h>

#include <algorithm>

#include "core/log.h"
#include "core/util.h"

namespace {

std::string column(sqlite3_stmt* st, int i) {
    const void* p = sqlite3_column_blob(st, i);
    int n = sqlite3_column_bytes(st, i);
    return p && n > 0 ? std::string(static_cast<const char*>(p), size_t(n)) : std::string();
}

// Finalizes the statement when leaving the scope.
struct Stmt {
    sqlite3_stmt* st;
    ~Stmt() { if (st) sqlite3_finalize(st); }
    operator sqlite3_stmt*() const { return st; }
    explicit operator bool() const { return st != nullptr; }
};

} // namespace

WpnStore::~WpnStore() {
    close();
    if (change_ != INVALID_HANDLE_VALUE) FindCloseChangeNotification(change_);
}

void WpnStore::close() {
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
}

bool WpnStore::open() {
    close();
    wchar_t* local = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return false;
    std::wstring dir = std::wstring(local) + L"\\Microsoft\\Windows\\Notifications";
    CoTaskMemFree(local);
    std::wstring path = dir + L"\\wpndatabase.db";
    if (!util::fileExists(path)) {
        LOG("wpn: %s not found", util::toUtf8(path).c_str());
        return false;
    }
    // Read-only: we never write, and never checkpoint the WAL of Windows' database.
    std::string uri = "file:///" + util::toUtf8(path) + "?mode=ro";
    for (char& c : uri)
        if (c == '\\') c = '/';
    int rc = sqlite3_open_v2(uri.c_str(), &db_, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nullptr);
    if (rc != SQLITE_OK) {
        LOG("wpn: open failed: %s", db_ ? sqlite3_errmsg(db_) : "?");
        close();
        return false;
    }
    sqlite3_busy_timeout(db_, 250);
    if (change_ == INVALID_HANDLE_VALUE)
        change_ = FindFirstChangeNotificationW(dir.c_str(), FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
    LOG("wpn: opened (sqlite %s)", sqlite3_libversion());
    return true;
}

void WpnStore::rearm() {
    if (change_ != INVALID_HANDLE_VALUE) FindNextChangeNotification(change_);
}

sqlite3_stmt* WpnStore::prepare(const char* sql) {
    if (!db_) return nullptr;
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &st, nullptr) != SQLITE_OK) {
        LOG("wpn: prepare failed: %s", sqlite3_errmsg(db_));
        return nullptr;
    }
    return st;
}

bool WpnStore::changed() {
    Stmt st{prepare("PRAGMA data_version")};
    if (!st || sqlite3_step(st) != SQLITE_ROW) return false;
    long long v = sqlite3_column_int64(st, 0);
    bool c = v != dataVersion_;
    dataVersion_ = v;
    return c;
}

std::vector<ToastRow> WpnStore::fetchNew() {
    std::vector<ToastRow> rows;
    if (lastOrder_ < 0) {
        Stmt st{prepare("SELECT IFNULL(MAX([Order]), 0) FROM Notification")};
        if (st && sqlite3_step(st) == SQLITE_ROW) lastOrder_ = sqlite3_column_int64(st, 0);
        return rows;
    }
    Stmt st{prepare("SELECT n.[Order], n.Id, h.PrimaryId, n.Payload, n.Tag, n.[Group] FROM Notification n "
                    "JOIN NotificationHandler h ON h.RecordId = n.HandlerId "
                    "WHERE n.Type = 'toast' AND n.[Order] > ? ORDER BY n.[Order]")};
    if (!st) return rows;
    sqlite3_bind_int64(st, 1, lastOrder_);
    while (sqlite3_step(st) == SQLITE_ROW) {
        ToastRow r;
        r.order = sqlite3_column_int64(st, 0);
        r.id = sqlite3_column_int64(st, 1);
        r.aumid = column(st, 2);
        r.payload = column(st, 3);
        r.tag = column(st, 4);
        r.group = column(st, 5);
        lastOrder_ = std::max(lastOrder_, r.order);
        rows.push_back(std::move(r));
    }
    return rows;
}

std::set<int64_t> WpnStore::toastIds() {
    std::set<int64_t> ids;
    Stmt st{prepare("SELECT Id FROM Notification WHERE Type = 'toast'")};
    if (!st) return ids;
    while (sqlite3_step(st) == SQLITE_ROW) ids.insert(sqlite3_column_int64(st, 0));
    return ids;
}

std::map<std::string, std::string> WpnStore::data(int64_t id) {
    std::map<std::string, std::string> m;
    Stmt st{prepare("SELECT [Key], Value FROM NotificationData WHERE NotificationId = ?")};
    if (!st) return m;
    sqlite3_bind_int64(st, 1, id);
    while (sqlite3_step(st) == SQLITE_ROW) m[column(st, 0)] = column(st, 1);
    return m;
}

bool WpnStore::bannerAllowed(const std::string& aumid) {
    Stmt st{prepare("SELECT s.SettingKey, s.Value FROM HandlerSettings s JOIN NotificationHandler h ON h.RecordId = s.HandlerId "
                    "WHERE h.PrimaryId = ? AND s.SettingKey IN ('s:banner', 's:toast')")};
    if (!st) return true;
    sqlite3_bind_text(st, 1, aumid.c_str(), int(aumid.size()), SQLITE_TRANSIENT);
    bool allowed = true;
    while (sqlite3_step(st) == SQLITE_ROW)
        if (sqlite3_column_type(st, 1) != SQLITE_NULL && sqlite3_column_int(st, 1) == 0) allowed = false;
    return allowed;
}

std::string WpnStore::asset(const std::string& aumid, const char* key) {
    Stmt st{prepare("SELECT a.AssetValue FROM HandlerAssets a JOIN NotificationHandler h ON h.RecordId = a.HandlerId "
                    "WHERE h.PrimaryId = ? AND a.AssetKey = ?")};
    if (!st) return {};
    sqlite3_bind_text(st, 1, aumid.c_str(), int(aumid.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, key, -1, SQLITE_TRANSIENT);
    return sqlite3_step(st) == SQLITE_ROW ? column(st, 0) : std::string();
}
