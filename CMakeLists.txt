cmake_minimum_required(VERSION 3.20)
project(SteamVrWindowsNotification CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: no Visual C++ redistributable needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
add_executable(SteamVrWindowsNotification WIN32 ${SOURCES})

target_include_directories(SteamVrWindowsNotification PRIVATE src third_party/openvr/headers third_party/stb)
# winsqlite3 ships with Windows: the notification database is read without bundling SQLite.
target_link_libraries(SteamVrWindowsNotification PRIVATE
    ${CMAKE_SOURCE_DIR}/third_party/openvr/lib/openvr_api.lib
    d3d11 dxgi winsqlite3 windowscodecs ole32 shell32 shlwapi urlmon ntdll)
target_compile_definitions(SteamVrWindowsNotification PRIVATE
    NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_compile_options(SteamVrWindowsNotification PRIVATE /W3 /utf-8 /permissive- $<$<CONFIG:Release>:/O1 /GS->)
target_link_options(SteamVrWindowsNotification PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>)

# Everything SteamVR needs lives in dist/ (the .vrmanifest points to it).
set(DIST_DIR ${CMAKE_SOURCE_DIR}/dist)
set_target_properties(SteamVrWindowsNotification PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY_RELEASE ${DIST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${DIST_DIR}
    RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/debug)

add_custom_command(TARGET SteamVrWindowsNotification POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/third_party/openvr/bin/openvr_api.dll $<TARGET_FILE_DIR:SteamVrWindowsNotification>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/resources $<TARGET_FILE_DIR:SteamVrWindowsNotification>)
