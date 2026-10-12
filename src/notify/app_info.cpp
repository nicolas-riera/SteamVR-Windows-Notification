#include "notify/app_info.h"

#include <windows.h>
#include <appmodel.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cwctype>
#include <vector>

#include "core/log.h"
#include "core/util.h"
#include "notify/wpn_store.h"

namespace {

bool istarts(const std::string& s, const char* p) { return _strnicmp(s.c_str(), p, strlen(p)) == 0; }

std::wstring regString(HKEY root, const std::wstring& key, const wchar_t* value) {
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    // REG_EXPAND_SZ values come back expanded.
    if (RegGetValueW(root, key.c_str(), value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return {};
    return buf;
}

// "@dll,-id" / "@{package?ms-resource://...}" -> text.
std::wstring indirect(const std::wstring& s) {
    if (s.empty() || s[0] != L'@') return s;
    wchar_t out[512] = {};
    return SUCCEEDED(SHLoadIndirectString(s.c_str(), out, ARRAYSIZE(out), nullptr)) ? std::wstring(out) : std::wstring();
}

std::wstring packageFamily(const std::string& aumid) {
    size_t bang = aumid.find('!');
    return bang == std::string::npos ? std::wstring() : util::fromUtf8(aumid.substr(0, bang));
}

std::wstring packagePath(const std::wstring& family) {
    if (family.empty()) return {};
    UINT32 count = 0, bufLen = 0;
    if (GetPackagesByPackageFamily(family.c_str(), &count, nullptr, &bufLen, nullptr) != ERROR_INSUFFICIENT_BUFFER || !count) return {};
    std::vector<PWSTR> names(count);
    std::vector<wchar_t> buf(bufLen);
    if (GetPackagesByPackageFamily(family.c_str(), &count, names.data(), &bufLen, buf.data()) != ERROR_SUCCESS) return {};
    UINT32 len = 0;
    GetPackagePathByFullName(names[0], &len, nullptr);
    std::wstring path(len, L'\0');
    if (GetPackagePathByFullName(names[0], &len, path.data()) != ERROR_SUCCESS) return {};
    path.resize(wcslen(path.c_str()));
    return path;
}

std::wstring percentDecode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            out += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else out += s[i];
    }
    std::wstring w = util::fromUtf8(out);
    std::replace(w.begin(), w.end(), L'/', L'\\');
    return w;
}

// Packaged assets are often stored only with resource qualifiers
// (Logo.scale-200.png, Logo.targetsize-48_altform-unplated.png): pick the best.
std::wstring qualified(const std::wstring& path) {
    if (util::fileExists(path)) return path;
    size_t slash = path.find_last_of(L'\\'), dot = path.find_last_of(L'.');
    if (slash == std::wstring::npos || dot == std::wstring::npos || dot < slash) return {};
    std::wstring dir = path.substr(0, slash + 1), stem = path.substr(slash + 1, dot - slash - 1), ext = path.substr(dot);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + stem + L".*" + ext).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    std::wstring best;
    int bestScore = -1;
    do {
        std::wstring n = fd.cFileName;
        std::transform(n.begin(), n.end(), n.begin(), ::towlower);
        if (n.find(L"contrast-") != std::wstring::npos) continue;
        int score = 1;
        size_t p;
        if ((p = n.find(L"targetsize-")) != std::wstring::npos) score = std::min(_wtoi(n.c_str() + p + 11), 256) * 4;
        else if ((p = n.find(L"scale-")) != std::wstring::npos) score = std::min(_wtoi(n.c_str() + p + 6), 400);
        if (n.find(L"unplated") != std::wstring::npos) score += 2;
        if (score > bestScore) { bestScore = score; best = dir + fd.cFileName; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return best;
}

bool shellInfo(const std::string& aumid, int px, std::string& name, Image& icon) {
    std::wstring parse = L"shell:AppsFolder\\" + util::fromUtf8(aumid);
    IShellItem* item = nullptr;
    if (FAILED(SHCreateItemFromParsingName(parse.c_str(), nullptr, IID_PPV_ARGS(&item)))) return false;
    if (name.empty()) {
        PWSTR dn = nullptr;
        if (SUCCEEDED(item->GetDisplayName(SIGDN_NORMALDISPLAY, &dn))) {
            name = util::toUtf8(dn);
            CoTaskMemFree(dn);
        }
    }
    if (icon.empty()) {
        IShellItemImageFactory* f = nullptr;
        if (SUCCEEDED(item->QueryInterface(IID_PPV_ARGS(&f)))) {
            HBITMAP bmp = nullptr;
            if (SUCCEEDED(f->GetImage(SIZE{px, px}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bmp)) && bmp) {
                if (imageFromBitmap(bmp, icon)) downscale(icon, px, px);
                DeleteObject(bmp);
            }
            f->Release();
        }
    }
    item->Release();
    return true;
}

} // namespace

bool isRemoteUri(const std::string& src) { return istarts(src, "http://") || istarts(src, "https://"); }

std::wstring resolveLocalImage(const std::string& src, const std::string& aumid) {
    if (src.empty() || isRemoteUri(src)) return {};
    if (istarts(src, "file:///")) return qualified(percentDecode(src.substr(8)));
    if (istarts(src, "file://")) return qualified(L"\\\\" + percentDecode(src.substr(7))); // UNC
    if (istarts(src, "ms-appx:///") || istarts(src, "ms-appx-web:///")) {
        std::wstring root = packagePath(packageFamily(aumid));
        if (root.empty()) return {};
        return qualified(root + L"\\" + percentDecode(src.substr(src.find(":///") + 4)));
    }
    if (istarts(src, "ms-appdata:///")) {
        std::wstring family = packageFamily(aumid);
        if (family.empty()) return {};
        std::string rest = src.substr(14);
        const char* folder = nullptr;
        if (istarts(rest, "local/")) folder = "LocalState";
        else if (istarts(rest, "roaming/")) folder = "RoamingState";
        else if (istarts(rest, "temp/")) folder = "TempState";
        if (!folder) return {};
        wchar_t* local = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return {};
        std::wstring base = std::wstring(local) + L"\\Packages\\" + family + L"\\" + util::fromUtf8(folder) + L"\\";
        CoTaskMemFree(local);
        return qualified(base + percentDecode(rest.substr(rest.find('/') + 1)));
    }
    // Plain path ("C:\...", maybe with forward slashes).
    std::wstring p = util::fromUtf8(src);
    std::replace(p.begin(), p.end(), L'/', L'\\');
    if (p.size() > 2 && (p[1] == L':' || (p[0] == L'\\' && p[1] == L'\\'))) return qualified(p);
    return {};
}

const AppInfo& AppDirectory::lookup(const std::string& aumid, WpnStore& store, int iconPx) {
    auto it = cache_.find(aumid);
    if (it != cache_.end()) return it->second;
    AppInfo info;
    const std::wstring regKey = L"AppUserModelId\\" + util::fromUtf8(aumid);

    // 1. What the app registered with the notification platform.
    info.name = store.asset(aumid, "DisplayName");
    std::string iconUri = store.asset(aumid, "IconUri");
    // 2. Registry registration (desktop apps, Windows' own system toasts).
    if (info.name.empty()) info.name = util::toUtf8(indirect(regString(HKEY_CLASSES_ROOT, regKey, L"DisplayName")));
    if (iconUri.empty()) iconUri = util::toUtf8(regString(HKEY_CLASSES_ROOT, regKey, L"IconUri"));
    if (!iconUri.empty()) {
        std::wstring path = resolveLocalImage(iconUri, aumid);
        if (!path.empty()) loadImage(path, iconPx, iconPx, info.icon);
    }
    // 3. Start menu entry (packaged apps and shortcuts carrying an AUMID).
    if (info.name.empty() || info.icon.empty()) shellInfo(aumid, iconPx, info.name, info.icon);
    if (info.name.empty()) {
        // Last resort: "...\\app.exe" -> "app", "Company.App" -> "App".
        std::string n = aumid.substr(aumid.find_last_of("\\!") + 1);
        if (n.size() > 4 && _stricmp(n.c_str() + n.size() - 4, ".exe") == 0) n.resize(n.size() - 4);
        else if (n.find('.') != std::string::npos) n = n.substr(n.find_last_of('.') + 1);
        info.name = n;
    }
    LOG("app: %s -> '%s', icon %dx%d", aumid.c_str(), info.name.c_str(), info.icon.w, info.icon.h);
    if (cache_.size() > 64) cache_.clear();
    return cache_.emplace(aumid, std::move(info)).first->second;
}
