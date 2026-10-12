#pragma once
#include <string>

namespace util {

std::wstring exeDir();
std::string toUtf8(const std::wstring& w);
std::wstring fromUtf8(const std::string& s);
bool readFile(const std::wstring& path, std::string& out);
bool writeFile(const std::wstring& path, const std::string& data);
bool fileExists(const std::wstring& path);

// Monotonic clock, seconds.
double now();

std::string format(const char* fmt, ...);
// "HH:MM:SS"
std::string timecode(double ms);

} // namespace util
