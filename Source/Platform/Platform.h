#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

//-------------------------------------------------------------------------
// Thin Win32 wrappers: windows in front, idle time, the Run key, registry reads and process launch.
//-------------------------------------------------------------------------

namespace Kui::Platform
{
    // Window class of the main window, so a second launch can find and focus it
    constexpr wchar_t g_mainWindowClass[] = L"KuiMainWindow";

    // Our own exe name, never tracked
    constexpr char g_selfExe[] = "kui";

    //-------------------------------------------------------------------------

    struct WindowInfo
    {
        std::string     m_app;      // Exe name without extension
        std::string     m_title;
    };

    //-------------------------------------------------------------------------

    // The window in front
    std::optional<WindowInfo> ActiveWindow();

    // Every real, visible top-level window except ours (minimized counts, tray-hidden doesn't)
    std::vector<WindowInfo> OpenWindows();

    // Distinct programs of OpenWindows(), sorted case-insensitively
    std::vector<std::string> OpenPrograms();

    // Seconds since the last keyboard or mouse input
    int64_t IdleSecs();

    // Is the tracker in the HKCU Run key?
    bool IsAutostartEnabled();
    void SetAutostart( bool on );

    // Full path of this exe
    std::wstring ExePath();

    // Starts `kui.exe --tracker`
    void SpawnTracker();

    // Restores and focuses an open main window. False if there is none.
    bool FocusMainWindow();

    // Focuses the main window, or starts it when it isn't open
    void OpenApp();

    // Opens Explorer with the file selected
    void RevealInExplorer( std::filesystem::path const& file );
    void OpenFolder( std::filesystem::path const& dir );

    // Names of the subkeys of a registry key
    std::vector<std::string> RegSubkeys( HKEY root, std::string const& path );

    // A REG_SZ value
    std::optional<std::string> RegString( HKEY root, std::string const& path, std::string const& value );
}
