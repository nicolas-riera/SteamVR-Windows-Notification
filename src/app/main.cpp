#include <windows.h>
#include <shellapi.h>

#include <string>

#include "app/app.h"

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    int rc;
    if (argc >= 3 && std::wstring(argv[1]) == L"--snapshot") rc = runSnapshot(argv[2]);
    else if (argc >= 2 && std::wstring(argv[1]) == L"--unregister") rc = runUnregister();
    else rc = runOverlay();
    LocalFree(argv);
    return rc;
}
