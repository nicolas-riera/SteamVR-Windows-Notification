#pragma once
// http(s) images of toasts, fetched through the WinINet cache on short-lived
// threads (rare: most apps reference local files).
#include <windows.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>

class Downloader {
public:
    Downloader();
    ~Downloader();
    // Signalled when a download finishes.
    HANDLE event() const;
    // Starts the download on first call. Returns true once finished, with the
    // local file in `path` ("" on failure).
    bool get(const std::string& url, std::wstring& path);

private:
    struct Shared {
        std::mutex m;
        std::map<std::string, std::wstring> done;
        HANDLE event = nullptr;
        ~Shared() { if (event) CloseHandle(event); }
    };
    std::shared_ptr<Shared> shared_;
    std::map<std::string, bool> started_;
};
