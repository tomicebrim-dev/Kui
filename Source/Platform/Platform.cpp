#include "Platform/Platform.h"

#include <dwmapi.h>
#include <shellapi.h>

#include <algorithm>

#include "Core/Text.h"

//-------------------------------------------------------------------------

namespace Kui::Platform
{
    constexpr wchar_t g_runKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t g_runValue[] = L"Kui";

    // ponytail: program name is the .exe name; the original prefers the exe's FileDescription.
    static std::optional<std::string> AppOf( HWND hWnd )
    {
        DWORD pid = 0;
        GetWindowThreadProcessId( hWnd, &pid );
        if( pid == 0 )
        {
            return std::nullopt;
        }

        HANDLE hProcess = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid );
        if( hProcess == nullptr )
        {
            return std::nullopt;
        }

        wchar_t buffer[1024];
        DWORD length = static_cast<DWORD>( std::size( buffer ) );
        BOOL const ok = QueryFullProcessImageNameW( hProcess, 0, buffer, &length );
        CloseHandle( hProcess );
        if( !ok )
        {
            return std::nullopt;
        }

        return Core::Narrow( std::filesystem::path( std::wstring( buffer, length ) ).stem().wstring() );
    }

    static std::string TitleOf( HWND hWnd )
    {
        wchar_t buffer[1024];
        int const length = GetWindowTextW( hWnd, buffer, static_cast<int>( std::size( buffer ) ) );
        return Core::Narrow( std::wstring_view( buffer, std::max( length, 0 ) ) );
    }

    static BOOL CALLBACK CollectWindow( HWND hWnd, LPARAM lParam )
    {
        auto* pOut = reinterpret_cast<std::vector<WindowInfo>*>( lParam );

        // Cloaked: hidden UWP shells and other virtual-desktop ghosts
        DWORD cloaked = 0;
        DwmGetWindowAttribute( hWnd, DWMWA_CLOAKED, &cloaked, sizeof( cloaked ) );
        bool const isToolWindow = ( GetWindowLongW( hWnd, GWL_EXSTYLE ) & WS_EX_TOOLWINDOW ) != 0;
        if( !IsWindowVisible( hWnd ) || cloaked != 0 || isToolWindow || GetWindow( hWnd, GW_OWNER ) != nullptr || GetWindowTextLengthW( hWnd ) == 0 )
        {
            return TRUE;
        }

        std::optional<std::string> app = AppOf( hWnd );
        if( app && !Core::IEqualsAscii( *app, g_selfExe ) )
        {
            pOut->push_back( { *app, TitleOf( hWnd ) } );
        }
        return TRUE;
    }

    //-------------------------------------------------------------------------

    std::optional<WindowInfo> ActiveWindow()
    {
        HWND hWnd = GetForegroundWindow();
        if( hWnd == nullptr )
        {
            return std::nullopt;
        }

        std::optional<std::string> app = AppOf( hWnd );
        if( !app )
        {
            return std::nullopt;
        }
        return WindowInfo { *app, TitleOf( hWnd ) };
    }

    std::vector<WindowInfo> OpenWindows()
    {
        std::vector<WindowInfo> windows;
        EnumWindows( CollectWindow, reinterpret_cast<LPARAM>( &windows ) );
        return windows;
    }

    std::vector<std::string> OpenPrograms()
    {
        std::vector<std::string> programs;
        for( WindowInfo const& w : OpenWindows() )
        {
            if( std::find( programs.begin(), programs.end(), w.m_app ) == programs.end() )
            {
                programs.push_back( w.m_app );
            }
        }
        std::sort( programs.begin(), programs.end(), []( std::string const& a, std::string const& b ) { return Core::ToLower( a ) < Core::ToLower( b ); } );
        return programs;
    }

    int64_t IdleSecs()
    {
        LASTINPUTINFO info { sizeof( LASTINPUTINFO ), 0 };
        if( !GetLastInputInfo( &info ) )
        {
            return 0;
        }
        return static_cast<int64_t>( ( GetTickCount() - info.dwTime ) / 1000 );
    }

    bool IsAutostartEnabled()
    {
        return RegGetValueW( HKEY_CURRENT_USER, g_runKey, g_runValue, RRF_RT_REG_SZ, nullptr, nullptr, nullptr ) == ERROR_SUCCESS;
    }

    void SetAutostart( bool on )
    {
        if( !on )
        {
            RegDeleteKeyValueW( HKEY_CURRENT_USER, g_runKey, g_runValue );
            return;
        }

        std::wstring const command = L"\"" + ExePath() + L"\" --tracker";
        DWORD const bytes = static_cast<DWORD>( ( command.size() + 1 ) * sizeof( wchar_t ) );
        RegSetKeyValueW( HKEY_CURRENT_USER, g_runKey, g_runValue, REG_SZ, command.c_str(), bytes );
    }

    std::wstring ExePath()
    {
        wchar_t buffer[MAX_PATH * 4];
        DWORD const length = GetModuleFileNameW( nullptr, buffer, static_cast<DWORD>( std::size( buffer ) ) );
        return std::wstring( buffer, length );
    }

    void SpawnTracker()
    {
        ShellExecuteW( nullptr, nullptr, ExePath().c_str(), L"--tracker", nullptr, SW_SHOWNORMAL );
    }

    bool FocusMainWindow()
    {
        HWND hWnd = FindWindowW( g_mainWindowClass, nullptr );
        if( hWnd == nullptr )
        {
            return false;
        }

        ShowWindow( hWnd, IsIconic( hWnd ) ? SW_RESTORE : SW_SHOW );
        SetForegroundWindow( hWnd );
        return true;
    }

    void OpenApp()
    {
        if( !FocusMainWindow() )
        {
            ShellExecuteW( nullptr, nullptr, ExePath().c_str(), nullptr, nullptr, SW_SHOWNORMAL );
        }
    }

    void RevealInExplorer( std::filesystem::path const& file )
    {
        std::wstring const args = L"/select,\"" + file.wstring() + L"\"";
        ShellExecuteW( nullptr, nullptr, L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL );
    }

    void OpenFolder( std::filesystem::path const& dir )
    {
        ShellExecuteW( nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL );
    }

    std::vector<std::string> RegSubkeys( HKEY root, std::string const& path )
    {
        std::vector<std::string> names;
        HKEY hKey = nullptr;
        if( RegOpenKeyExW( root, Core::Widen( path ).c_str(), 0, KEY_READ, &hKey ) != ERROR_SUCCESS )
        {
            return names;
        }

        wchar_t buffer[256];
        for( DWORD i = 0;; i++ )
        {
            DWORD length = static_cast<DWORD>( std::size( buffer ) );
            if( RegEnumKeyExW( hKey, i, buffer, &length, nullptr, nullptr, nullptr, nullptr ) != ERROR_SUCCESS )
            {
                break;
            }
            names.push_back( Core::Narrow( std::wstring_view( buffer, length ) ) );
        }

        RegCloseKey( hKey );
        return names;
    }

    std::optional<std::string> RegString( HKEY root, std::string const& path, std::string const& value )
    {
        std::wstring const widePath = Core::Widen( path );
        std::wstring const wideValue = Core::Widen( value );
        DWORD bytes = 0;
        if( RegGetValueW( root, widePath.c_str(), wideValue.c_str(), RRF_RT_REG_SZ, nullptr, nullptr, &bytes ) != ERROR_SUCCESS )
        {
            return std::nullopt;
        }

        std::wstring buffer( bytes / sizeof( wchar_t ) + 1, L'\0' );
        bytes = static_cast<DWORD>( buffer.size() * sizeof( wchar_t ) );
        if( RegGetValueW( root, widePath.c_str(), wideValue.c_str(), RRF_RT_REG_SZ, nullptr, buffer.data(), &bytes ) != ERROR_SUCCESS )
        {
            return std::nullopt;
        }

        buffer.resize( wcsnlen( buffer.c_str(), buffer.size() ) );
        return Core::Narrow( buffer );
    }
}
