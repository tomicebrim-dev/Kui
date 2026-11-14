#include "Tracker/Tracker.h"

#include <windows.h>
#include <shellapi.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "Core/Text.h"
#include "Core/TimeUtil.h"
#include "Platform/Platform.h"

//-------------------------------------------------------------------------

namespace Kui::Tracker
{
    constexpr UINT g_trayMessage = WM_APP + 1;
    constexpr UINT g_safeModeMessage = WM_APP + 2; // Poll thread -> tray window: the safe-mode state changed
    constexpr UINT g_menuOpen = 1;
    constexpr UINT g_menuExit = 2;
    constexpr UINT g_menuSettings = 3;

    // Seconds between two 1 s ticks that can only mean sleep or hibernate (or the clock jumped)
    constexpr int64_t g_sleepGapSecs = 30;

    // Set by the poll thread, read by the tray window for its tooltip
    static std::atomic<bool> g_safeMode = false;

    // A focus wait: the rule that matched and when it first did
    struct Pending
    {
        uint64_t    m_rule = 0;
        int64_t     m_since = 0;
    };

    // A rule timer would otherwise keep "running" until the next start
    static void StopRuleTimer( Core::Store const& store )
    {
        std::optional<Core::Timer> const timer = store.LoadTimer();
        if( timer && timer->m_rule )
        {
            store.StopTimer( Core::Now() );
        }
    }

    static void AddTrayIcon( HWND hWnd )
    {
        NOTIFYICONDATAW data { sizeof( data ) };
        data.hWnd = hWnd;
        data.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        data.uCallbackMessage = g_trayMessage;
        data.hIcon = static_cast<HICON>( LoadImageW( GetModuleHandleW( nullptr ), MAKEINTRESOURCEW( 1 ), IMAGE_ICON, GetSystemMetrics( SM_CXSMICON ), GetSystemMetrics( SM_CYSMICON ), 0 ) );
        wcscpy_s( data.szTip, g_safeMode ? L"Kui - anti-cheat detected: light scan" : L"Kui" );
        Shell_NotifyIconW( NIM_ADD, &data );
    }

    static void UpdateTrayTip( HWND hWnd )
    {
        NOTIFYICONDATAW data { sizeof( data ) };
        data.hWnd = hWnd;
        data.uFlags = NIF_TIP;
        wcscpy_s( data.szTip, g_safeMode ? L"Kui - anti-cheat detected: light scan" : L"Kui" );
        Shell_NotifyIconW( NIM_MODIFY, &data );
    }

    static void RemoveTrayIcon( HWND hWnd )
    {
        NOTIFYICONDATAW data { sizeof( data ) };
        data.hWnd = hWnd;
        Shell_NotifyIconW( NIM_DELETE, &data );
    }

    // Dark popup menus. ponytail: undocumented uxtheme ordinals 135/136 (Win10 1903+), owner-drawn menu if they ever go away.
    static void UseDarkMenus()
    {
        HMODULE const hUxTheme = LoadLibraryExW( L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 );
        if( hUxTheme == nullptr )
        {
            return;
        }

        using SetPreferredAppMode = int( WINAPI* )( int );
        using FlushMenuThemes = void( WINAPI* )();
        auto const pSetMode = reinterpret_cast<SetPreferredAppMode>( GetProcAddress( hUxTheme, MAKEINTRESOURCEA( 135 ) ) );
        auto const pFlush = reinterpret_cast<FlushMenuThemes>( GetProcAddress( hUxTheme, MAKEINTRESOURCEA( 136 ) ) );
        if( pSetMode != nullptr && pFlush != nullptr )
        {
            pSetMode( 2 );  // ForceDark
            pFlush();
        }
    }

    static void ShowTrayMenu( HWND hWnd )
    {
        HMENU hMenu = CreatePopupMenu();
        AppendMenuW( hMenu, MF_STRING, g_menuOpen, L"Open Kui" );
        AppendMenuW( hMenu, MF_STRING, g_menuSettings, L"Settings" );
        AppendMenuW( hMenu, MF_SEPARATOR, 0, nullptr );
        AppendMenuW( hMenu, MF_STRING, g_menuExit, L"Exit tracker" );

        // Foreground first, or the menu won't close when clicking elsewhere
        POINT cursor;
        GetCursorPos( &cursor );
        SetForegroundWindow( hWnd );
        UINT const command = TrackPopupMenu( hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, hWnd, nullptr );
        PostMessageW( hWnd, WM_NULL, 0, 0 );
        DestroyMenu( hMenu );

        if( command == g_menuOpen || command == g_menuSettings )
        {
            Platform::OpenApp( command == g_menuSettings );
        }
        else if( command == g_menuExit )
        {
            DestroyWindow( hWnd );
        }
    }

    static LRESULT CALLBACK TrayWndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
    {
        auto const* pStore = reinterpret_cast<Core::Store const*>( GetWindowLongPtrW( hWnd, GWLP_USERDATA ) );

        // Explorer restarted: the tray forgot every icon
        static UINT const s_taskbarCreated = RegisterWindowMessageW( L"TaskbarCreated" );
        if( msg == s_taskbarCreated )
        {
            AddTrayIcon( hWnd );
            return 0;
        }

        switch( msg )
        {
            case WM_CREATE:
            {
                auto const* pCreate = reinterpret_cast<CREATESTRUCTW const*>( lParam );
                SetWindowLongPtrW( hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( pCreate->lpCreateParams ) );
                AddTrayIcon( hWnd );
            }
            return 0;

            case g_safeModeMessage:
            {
                UpdateTrayTip( hWnd );
            }
            return 0;

            case g_trayMessage:
            {
                if( lParam == WM_LBUTTONUP )
                {
                    Platform::OpenApp();
                }
                else if( lParam == WM_RBUTTONUP )
                {
                    ShowTrayMenu( hWnd );
                }
            }
            return 0;

            // The installer closes us with WM_CLOSE; sign-out ends the session
            case WM_ENDSESSION:
            {
                if( wParam )
                {
                    StopRuleTimer( *pStore );
                }
            }
            return 0;

            case WM_CLOSE:
            {
                DestroyWindow( hWnd );
            }
            return 0;

            case WM_DESTROY:
            {
                StopRuleTimer( *pStore );
                RemoveTrayIcon( hWnd );
                PostQuitMessage( 0 );
            }
            return 0;
        }

        return DefWindowProcW( hWnd, msg, wParam, lParam );
    }

    // One pass per second over the window in front and the rules
    static void PollLoop( Core::Store store )
    {
        Core::Settings settings = store.LoadSettings();
        auto settingsTime = store.Modified( "settings.json" );
        std::optional<Platform::WindowInfo> lastSeen;
        std::optional<Pending> pending;
        std::vector<std::string> recent = store.LoadRecent();
        int64_t lastBeat = 0;
        int64_t lastScan = 0;
        int64_t lastAntiCheatCheck = 0;
        bool antiCheat = false; // Safe mode: a game's anti-cheat is running, so skip the all-window scan
        int64_t lastTick = Core::Now();

        while( true )
        {
            std::this_thread::sleep_for( std::chrono::milliseconds( 1000 ) );
            int64_t const t = Core::Now();

            // A long gap between ticks means the PC slept: a rule timer ends when it went to sleep.
            // Idle time can't catch this when the wake-up keypress resets it.
            if( t - lastTick > g_sleepGapSecs )
            {
                std::optional<Core::Timer> const timer = store.LoadTimer();
                if( timer && timer->m_rule )
                {
                    store.StopTimer( std::max( lastTick, timer->m_start ) );
                }
                pending.reset();
                lastSeen.reset();
            }
            lastTick = t;

            auto const modified = store.Modified( "settings.json" );
            if( modified != settingsTime )
            {
                settings = store.LoadSettings();
                settingsTime = modified;
                lastSeen.reset(); // Re-check the window against the new rules
            }

            if( t - lastAntiCheatCheck >= 5 )
            {
                lastAntiCheatCheck = t;
                bool const wasAntiCheat = antiCheat;
                antiCheat = Platform::AntiCheatActive();
                if( antiCheat != wasAntiCheat )
                {
                    g_safeMode = antiCheat;
                    PostMessageW( FindWindowW( L"KuiTracker", nullptr ), g_safeModeMessage, 0, 0 );
                }
            }

            Platform::WindowInfo const window = Platform::ActiveWindow().value_or( Platform::WindowInfo {} );
            bool const isSelf = Core::IEqualsAscii( window.m_app, Platform::g_selfExe );
            if( !isSelf && Core::Store::BumpRecent( recent, window.m_app ) )
            {
                store.SaveRecent( recent );
            }

            if( t - lastBeat >= g_heartbeatSecs )
            {
                store.SaveStatus( { t, window.m_app, window.m_title, antiCheat } );
                lastBeat = t;
            }

            if( !settings.m_tracking )
            {
                pending.reset();
                continue;
            }

            int64_t const idle = Platform::IdleSecs();
            if( settings.m_idleOn && idle >= static_cast<int64_t>( settings.m_idleMinutes ) * 60 )
            {
                std::optional<Core::Timer> const timer = store.LoadTimer();
                if( timer && timer->m_rule )
                {
                    store.StopTimer( t - idle );
                }
                pending.reset();
                lastSeen.reset(); // When they come back, the window in front is checked again
                continue;
            }

            // One read per tick; the window may change timer.json between ticks
            std::optional<Core::Timer> const timer = store.LoadTimer();
            std::optional<uint64_t> runningRule = timer ? timer->m_rule : std::nullopt;

            // The tracked program was closed: end its timer, whatever "stop on no match" says.
            // ponytail: window scan every 5 s (EnumWindows + a process snapshot is the tracker's priciest call); end time can be up to 5 s late.
            if( runningRule )
            {
                auto const rule = std::find_if( settings.m_rules.begin(), settings.m_rules.end(), [&]( Core::Rule const& r ) { return r.m_id == *runningRule; } );
                auto isOpen = [&]( Platform::WindowInfo const& w ) { return rule->Matches( Core::ToLower( w.MatchApp() + " " + w.m_title ) ); };
                if( rule != settings.m_rules.end() && !isOpen( window ) && !antiCheat && t - lastScan >= 5 )
                {
                    lastScan = t;
                    std::vector<Platform::WindowInfo> const windows = Platform::OpenWindows();
                    if( std::none_of( windows.begin(), windows.end(), isOpen ) )
                    {
                        store.StopTimer( t );
                        runningRule.reset();
                    }
                }
            }

            // Like the original, rules are checked when the window changes (or a focus wait is pending),
            // so stopping a rule timer by hand sticks until you switch windows.
            bool const isSameWindow = lastSeen && lastSeen->m_app == window.m_app && lastSeen->m_title == window.m_title;
            if( isSameWindow && !pending )
            {
                continue;
            }

            lastSeen = window;
            if( isSelf )
            {
                continue;
            }

            Core::Rule const* pRule = Core::FindRule( settings.m_rules, window.MatchApp(), window.m_title );
            if( pRule == nullptr )
            {
                pending.reset();
                if( settings.m_stopOnNoMatch && runningRule )
                {
                    store.StopTimer( t );
                }
                continue;
            }

            if( runningRule == pRule->m_id )
            {
                pending.reset();
                continue;
            }

            int64_t const since = ( pending && pending->m_rule == pRule->m_id ) ? pending->m_since : t;
            if( settings.m_focusOn && t - since < settings.m_focusSecs )
            {
                pending = Pending { pRule->m_id, since };
                continue;
            }

            store.StartTimer( { t, pRule->Label(), pRule->m_project, pRule->m_id } );
            pending.reset();
        }
    }

    //-------------------------------------------------------------------------

    bool IsAlive( std::optional<Core::Status> const& status )
    {
        return status && Core::Now() - status->m_heartbeat <= g_heartbeatSecs * 2;
    }

    int Run( Core::Store const& store )
    {
        // One tracker per user session. The handle is released when the process ends.
        CreateMutexW( nullptr, TRUE, L"Local\\KuiTracker" );
        if( GetLastError() == ERROR_ALREADY_EXISTS )
        {
            return 0;
        }

        // A rule timer left running by a tracker that never shut down (crash, power cut) ends at its last heartbeat
        std::optional<Core::Timer> const timer = store.LoadTimer();
        std::optional<Core::Status> const status = store.LoadStatus();
        if( timer && status && timer->m_rule && Core::Now() - status->m_heartbeat > g_heartbeatSecs * 3 )
        {
            store.StopTimer( std::max( status->m_heartbeat, timer->m_start ) );
        }

        // Detached: the process exits from the message loop below and takes the thread with it
        std::thread( PollLoop, store ).detach();

        UseDarkMenus();
        WNDCLASSW windowClass {};
        windowClass.lpfnWndProc = TrayWndProc;
        windowClass.hInstance = GetModuleHandleW( nullptr );
        windowClass.lpszClassName = L"KuiTracker";
        RegisterClassW( &windowClass );

        // Hidden top-level window (not message-only) so it gets TaskbarCreated and the installer's WM_CLOSE
        CreateWindowExW( 0, windowClass.lpszClassName, L"Kui tracker", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr, windowClass.hInstance, const_cast<Core::Store*>( &store ) );

        MSG msg;
        while( GetMessageW( &msg, nullptr, 0, 0 ) > 0 )
        {
            TranslateMessage( &msg );
            DispatchMessageW( &msg );
        }
        return 0;
    }
}
