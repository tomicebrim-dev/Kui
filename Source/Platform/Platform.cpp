#include "Platform/Platform.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <format>
#include <mutex>
#include <unordered_map>

#include "Core/Text.h"

//-------------------------------------------------------------------------

namespace Kui::Platform
{
    constexpr wchar_t g_runKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t g_runValue[] = L"Kui";

    // Exe stem by pid, from one process snapshot.
    // Anti-cheat safety: this never opens a handle to another process (no OpenProcess), so protected game processes see no access attempt from us.
    // ponytail: one snapshot per call, cache if profiling says so.
    static std::unordered_map<DWORD, std::wstring> ProcessNames()
    {
        std::unordered_map<DWORD, std::wstring> names;
        HANDLE hSnapshot = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
        if( hSnapshot == INVALID_HANDLE_VALUE )
        {
            return names;
        }

        PROCESSENTRY32W entry { sizeof( PROCESSENTRY32W ) };
        for( BOOL ok = Process32FirstW( hSnapshot, &entry ); ok; ok = Process32NextW( hSnapshot, &entry ) )
        {
            names[entry.th32ProcessID] = std::filesystem::path( entry.szExeFile ).stem().wstring();
        }

        CloseHandle( hSnapshot );
        return names;
    }

    // Full NT path of a pid's exe ("\Device\HarddiskVolumeN\..."), from a kernel query by pid.
    // Same data Task Manager and the process snapshot use: no handle to the process is opened.
    static std::optional<std::wstring> ImagePathOf( DWORD pid )
    {
        using QueryFn = LONG( NTAPI* )( ULONG, PVOID, ULONG, PULONG );
        static QueryFn const pQuery = reinterpret_cast<QueryFn>( reinterpret_cast<void*>( GetProcAddress( GetModuleHandleW( L"ntdll.dll" ), "NtQuerySystemInformation" ) ) );
        if( pQuery == nullptr )
        {
            return std::nullopt;
        }

        // SystemProcessIdInformation: { pid, UNICODE_STRING image name }
        constexpr ULONG systemProcessIdInformation = 88;
        struct ProcessIdInfo
        {
            HANDLE      m_pid;
            USHORT      m_length;
            USHORT      m_maxLength;
            PWSTR       m_pBuffer;
        };

        wchar_t buffer[1024];
        ProcessIdInfo info { reinterpret_cast<HANDLE>( static_cast<ULONG_PTR>( pid ) ), 0, sizeof( buffer ), buffer };
        if( pQuery( systemProcessIdInformation, &info, sizeof( info ), nullptr ) < 0 || info.m_length == 0 )
        {
            return std::nullopt;
        }

        return std::wstring( buffer, info.m_length / sizeof( wchar_t ) );
    }

    // The exe's FileDescription ("Google Chrome"), empty if it has none. Reads the exe file, not the process.
    static std::wstring ReadFileDescription( std::wstring const& ntPath )
    {
        std::wstring const file = L"\\\\?\\GLOBALROOT" + ntPath;
        DWORD handle = 0;
        DWORD const size = GetFileVersionInfoSizeW( file.c_str(), &handle );
        if( size == 0 )
        {
            return {};
        }

        std::vector<char> data( size );
        if( !GetFileVersionInfoW( file.c_str(), 0, size, data.data() ) )
        {
            return {};
        }

        struct Translation
        {
            WORD    m_language;
            WORD    m_codepage;
        };

        Translation const* pTranslation = nullptr;
        UINT length = 0;
        if( !VerQueryValueW( data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>( const_cast<Translation**>( &pTranslation ) ), &length ) || length < sizeof( Translation ) )
        {
            return {};
        }

        std::wstring const key = std::format( L"\\StringFileInfo\\{:04x}{:04x}\\FileDescription", pTranslation->m_language, pTranslation->m_codepage );
        wchar_t* pValue = nullptr;
        if( !VerQueryValueW( data.data(), key.c_str(), reinterpret_cast<void**>( &pValue ), &length ) || pValue == nullptr )
        {
            return {};
        }
        return std::wstring( pValue );
    }

    // Cached per exe path: the tracker and the UI both call in, and reading version info every second is wasteful
    static std::wstring FileDescriptionOf( std::wstring const& ntPath )
    {
        static std::mutex s_lock;
        static std::unordered_map<std::wstring, std::wstring> s_cache;

        std::lock_guard guard( s_lock );
        auto it = s_cache.find( ntPath );
        if( it == s_cache.end() )
        {
            it = s_cache.emplace( ntPath, ReadFileDescription( ntPath ) ).first;
        }
        return it->second;
    }

    // Program name: the exe's FileDescription, else its stem. Our own exe is always "kui" so it can be recognised.
    static WindowInfo AppOf( std::wstring const& stem, std::optional<std::wstring> const& ntPath )
    {
        std::string const name = Core::Narrow( stem );
        std::wstring const description = ntPath && !Core::IEqualsAscii( name, g_selfExe ) ? FileDescriptionOf( *ntPath ) : std::wstring();
        return WindowInfo { description.empty() ? name : Core::Narrow( description ), {}, name };
    }

    static std::string TitleOf( HWND hWnd )
    {
        wchar_t buffer[1024];
        int const length = GetWindowTextW( hWnd, buffer, static_cast<int>( std::size( buffer ) ) );
        return Core::Narrow( std::wstring_view( buffer, std::max( length, 0 ) ) );
    }

    struct CollectContext
    {
        std::unordered_map<DWORD, std::wstring>     m_names;
        std::vector<WindowInfo>                     m_windows;
    };

    static BOOL CALLBACK CollectWindow( HWND hWnd, LPARAM lParam )
    {
        auto* pContext = reinterpret_cast<CollectContext*>( lParam );

        // Cloaked: hidden UWP shells and other virtual-desktop ghosts
        DWORD cloaked = 0;
        DwmGetWindowAttribute( hWnd, DWMWA_CLOAKED, &cloaked, sizeof( cloaked ) );
        bool const isToolWindow = ( GetWindowLongW( hWnd, GWL_EXSTYLE ) & WS_EX_TOOLWINDOW ) != 0;
        if( !IsWindowVisible( hWnd ) || cloaked != 0 || isToolWindow || GetWindow( hWnd, GW_OWNER ) != nullptr || GetWindowTextLengthW( hWnd ) == 0 )
        {
            return TRUE;
        }

        DWORD pid = 0;
        GetWindowThreadProcessId( hWnd, &pid );
        auto const it = pContext->m_names.find( pid );
        if( pid == 0 || it == pContext->m_names.end() )
        {
            return TRUE;
        }

        WindowInfo window = AppOf( it->second, ImagePathOf( pid ) );
        if( !Core::IEqualsAscii( window.m_app, g_selfExe ) )
        {
            window.m_title = TitleOf( hWnd );
            pContext->m_windows.push_back( std::move( window ) );
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

        DWORD pid = 0;
        GetWindowThreadProcessId( hWnd, &pid );
        if( pid == 0 )
        {
            return std::nullopt;
        }

        // Called every second: one query by pid, not a whole process snapshot. The snapshot only if the query fails.
        std::optional<std::wstring> const ntPath = ImagePathOf( pid );
        std::wstring stem;
        if( ntPath )
        {
            stem = std::filesystem::path( *ntPath ).stem().wstring();
        }
        else
        {
            std::unordered_map<DWORD, std::wstring> const names = ProcessNames();
            auto const it = names.find( pid );
            if( it == names.end() )
            {
                return std::nullopt;
            }
            stem = it->second;
        }

        WindowInfo window = AppOf( stem, ntPath );
        window.m_title = TitleOf( hWnd );
        return window;
    }

    bool IsAntiCheatName( std::string const& app )
    {
        // ponytail: hand-kept list of common services/clients; add names as games turn up
        static char const* const names[] =
        {
            "easyanticheat", "easyanticheat_eos", "beservice", "beservice_x64", "vgc", "vgtray",
            "faceitclient", "faceit", "eseaclient", "gamemon", "xhunter1",
        };

        std::string const lower = Core::ToLower( app );
        return std::any_of( std::begin( names ), std::end( names ), [&]( char const* pName ) { return lower == pName; } );
    }

    bool AntiCheatActive()
    {
        for( auto const& [pid, name] : ProcessNames() )
        {
            if( IsAntiCheatName( Core::Narrow( name ) ) )
            {
                return true;
            }
        }
        return false;
    }

    std::vector<WindowInfo> OpenWindows()
    {
        CollectContext context { ProcessNames(), {} };
        EnumWindows( CollectWindow, reinterpret_cast<LPARAM>( &context ) );
        return context.m_windows;
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
