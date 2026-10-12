#include "core/log.h"

#include <windows.h>
#include <cstdarg>
#include <cstdio>

namespace logx {

static FILE* g_file = nullptr;
static long g_written = 0;
static constexpr long kMaxBytes = 512 * 1024; // keep the log tiny

void init(const std::wstring& path) {
    g_file = _wfopen(path.c_str(), L"w");
}

void write(const char* fmt, ...) {
    if (!g_file || g_written > kMaxBytes) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    g_written += fprintf(g_file, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list ap;
    va_start(ap, fmt);
    g_written += vfprintf(g_file, fmt, ap);
    va_end(ap);
    fputc('\n', g_file);
    fflush(g_file);
}

} // namespace logx
