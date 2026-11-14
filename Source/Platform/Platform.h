#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

//-------------------------------------------------------------------------
// Thin Win32 wrappers: windows in front, idle time, the Run key, registry reads and process launch.
//
// Anti-cheat safety: Kui may run beside protected games, so it never touches another process. No process handles, no memory access,
// no hooks, no injection, no input sniffing. Window titles, a process-name snapshot and a by-pid image-path query (NtQuerySystemInformation, no handle). Tests/AntiCheatTests.cpp enforces this on Source/.
//-------------------------------------------------------------------------

namespace Kui::Platform
{
    // Window class of the main window, so a second launch can find and focus it
    constexpr wchar_t g_mainWindowClass[] = L"KuiMainWindow";

    // Posted to the main window to open its settings popup
    constexpr UINT g_openSettingsMessage = WM_APP + 10;

    // Our own exe name, never tracked
    constexpr char g_selfExe[] = "kui";

    //-------------------------------------------------------------------------

    struct WindowInfo
    {
        std::string     m_app;      // FileDescription of the exe, else its name without extension
        std::string     m_title;
        std::string     m_exe;      // Exe name without extension, so rules written against it keep matching

        // What rules match against: the program name, plus the exe name when it differs
        std::string MatchApp() const { return m_exe.empty() || m_exe == m_app ? m_app : m_app + " " + m_exe; }
    };

    //-------------------------------------------------------------------------

    // The window in front
    std::optional<WindowInfo> ActiveWindow();

    // Every real, visible top-level window except ours (minimized counts, tray-hidden doesn't)
    std::vector<WindowInfo> OpenWindows();

    // Distinct programs of OpenWindows(), sorted case-insensitively
    std::vector<std::string> OpenPrograms();

    // Is this exe stem (no extension, any case) a known anti-cheat service or client?
    bool IsAntiCheatName( std::string const& app );

    // Is any known anti-cheat process running? Uses a process-name snapshot only.
    bool AntiCheatActive();

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

    // Focuses the main window, or starts it when it isn't open. `settings` also opens the settings popup.
    void OpenApp( bool settings = false );

    // Opens Explorer with the file selected
    void RevealInExplorer( std::filesystem::path const& file );
    void OpenFolder( std::filesystem::path const& dir );

    // Open dialog for a program's .exe (or a shortcut, resolved to its target). Nullopt when cancelled.
    std::optional<std::filesystem::path> PickProgram( HWND hOwner );

    // The exe file's FileDescription ("Google Chrome"), empty if it has none
    std::string ExeDescription( std::filesystem::path const& exe );

    // Names of the subkeys of a registry key
    std::vector<std::string> RegSubkeys( HKEY root, std::string const& path );

    // A REG_SZ value
    std::optional<std::string> RegString( HKEY root, std::string const& path, std::string const& value );
}
