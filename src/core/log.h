#pragma once
#include <string>

namespace logx {
void init(const std::wstring& path);
void write(const char* fmt, ...);
} // namespace logx

#define LOG(...) ::logx::write(__VA_ARGS__)
