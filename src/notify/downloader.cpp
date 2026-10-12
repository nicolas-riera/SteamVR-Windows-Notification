#include "notify/downloader.h"

#include <urlmon.h>

#include <thread>

#include "core/log.h"
#include "core/util.h"

Downloader::Downloader() : shared_(std::make_shared<Shared>()) {
    shared_->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
}

Downloader::~Downloader() = default; // running threads keep `shared_` alive

HANDLE Downloader::event() const { return shared_->event; }

bool Downloader::get(const std::string& url, std::wstring& path) {
    {
        std::lock_guard<std::mutex> lock(shared_->m);
        auto it = shared_->done.find(url);
        if (it != shared_->done.end()) {
            path = it->second;
            return true;
        }
    }
    if (started_[url]) return false;
    if (started_.size() > 64) started_.clear(); // finished results stay in `done`
    started_[url] = true;
    std::shared_ptr<Shared> s = shared_;
    std::thread([s, url] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        wchar_t file[MAX_PATH] = {};
        HRESULT hr = URLDownloadToCacheFileW(nullptr, util::fromUtf8(url).c_str(), file, MAX_PATH, 0, nullptr);
        CoUninitialize();
        if (FAILED(hr)) LOG("download failed (0x%08lx): %s", hr, url.c_str());
        std::lock_guard<std::mutex> lock(s->m);
        if (s->done.size() > 64) s->done.clear();
        s->done[url] = SUCCEEDED(hr) ? file : L"";
        SetEvent(s->event);
    }).detach();
    return false;
}
