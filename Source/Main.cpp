#include <windows.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "Core/Store.h"
#include "Core/Text.h"
#include "Core/TimeUtil.h"
#include "Platform/Platform.h"
#include "Tracker/Tracker.h"
#include "UI/App.h"
#include "UI/Theme.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

//-------------------------------------------------------------------------
// Entry point. `kui.exe` opens the window (and starts the tracker if it isn't running);
// `kui.exe --tracker` is the tray tracker. One exe for both.
//-------------------------------------------------------------------------

namespace Kui
{
    constexpr float g_windowWidth = 1100.0f;
    constexpr float g_windowHeight = 680.0f;
    constexpr float g_minWidth = 820.0f;
    constexpr float g_minHeight = 480.0f;

    // The mini timer window, and the grip strip on its left that drags it
    constexpr float g_miniWidth = 480.0f;
    constexpr float g_miniHeight = 74.0f;
    constexpr float g_miniGripWidth = 26.0f;

    // Frames kept running after input, so hovers and tooltips settle before going idle
    constexpr int g_busyFrames = 40;

    // Frames rendered before a --screenshot is saved
    constexpr int g_screenshotFrames = 10;

    //-------------------------------------------------------------------------

    // The D3D11 device and the window's swap chain
    struct Graphics
    {
        Microsoft::WRL::ComPtr<ID3D11Device>                m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>         m_context;
        Microsoft::WRL::ComPtr<IDXGISwapChain>              m_swapChain;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView>      m_renderTarget;
    };

    // State the window procedure needs, kept in GWLP_USERDATA
    struct WindowState
    {
        Core::Store const*      m_pStore = nullptr;
        bool                    m_isScreenshot = false;
        bool                    m_isMini = false;
        UINT                    m_resizeWidth = 0;
        UINT                    m_resizeHeight = 0;
        bool                    m_openSettings = false;
    };

    //-------------------------------------------------------------------------

    static void CreateRenderTarget( Graphics& graphics )
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        graphics.m_swapChain->GetBuffer( 0, IID_PPV_ARGS( &backBuffer ) );
        graphics.m_device->CreateRenderTargetView( backBuffer.Get(), nullptr, &graphics.m_renderTarget );
    }

    static bool CreateGraphics( HWND hWnd, Graphics& graphics )
    {
        DXGI_SWAP_CHAIN_DESC desc = {};
        desc.BufferCount = 2;
        desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.BufferDesc.RefreshRate.Numerator = 60;
        desc.BufferDesc.RefreshRate.Denominator = 1;
        desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.OutputWindow = hWnd;
        desc.SampleDesc.Count = 1;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        D3D_FEATURE_LEVEL const levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
        D3D_FEATURE_LEVEL level;
        HRESULT hr = D3D11CreateDeviceAndSwapChain( nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &desc, &graphics.m_swapChain, &graphics.m_device, &level, &graphics.m_context );
        if( hr == DXGI_ERROR_UNSUPPORTED )
        {
            // No usable GPU: software rasterizer
            hr = D3D11CreateDeviceAndSwapChain( nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &desc, &graphics.m_swapChain, &graphics.m_device, &level, &graphics.m_context );
        }

        if( FAILED( hr ) )
        {
            return false;
        }

        CreateRenderTarget( graphics );
        return true;
    }

    static float DpiScale( HWND hWnd )
    {
        return static_cast<float>( GetDpiForWindow( hWnd ) ) / USER_DEFAULT_SCREEN_DPI;
    }

    static void SavePlacement( HWND hWnd, Core::Store const& store )
    {
        WINDOWPLACEMENT wp = { sizeof( wp ) };
        if( !GetWindowPlacement( hWnd, &wp ) )
        {
            return;
        }

        RECT const& r = wp.rcNormalPosition;
        store.SavePlacement( Core::Placement{ r.left, r.top, r.right - r.left, r.bottom - r.top, wp.showCmd == SW_SHOWMAXIMIZED } );
    }

    // Restores the last position unless it's off every monitor now. Returns the show command to use.
    static int RestorePlacement( HWND hWnd, Core::Store const& store )
    {
        std::optional<Core::Placement> const p = store.LoadPlacement();
        if( !p.has_value() )
        {
            return SW_SHOWNORMAL;
        }

        RECT const r = { p->m_x, p->m_y, p->m_x + p->m_width, p->m_y + p->m_height };
        if( MonitorFromRect( &r, MONITOR_DEFAULTTONULL ) == nullptr )
        {
            return SW_SHOWNORMAL;
        }

        WINDOWPLACEMENT wp = { sizeof( wp ) };
        wp.showCmd = SW_HIDE;
        wp.rcNormalPosition = r;
        SetWindowPlacement( hWnd, &wp );
        return p->m_isMaximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
    }

    static LRESULT CALLBACK WndProc( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam )
    {
        if( ImGui_ImplWin32_WndProcHandler( hWnd, msg, wParam, lParam ) )
        {
            return TRUE;
        }

        WindowState* pState = reinterpret_cast<WindowState*>( GetWindowLongPtrW( hWnd, GWLP_USERDATA ) );
        switch( msg )
        {
            case WM_SIZE:
            {
                if( pState != nullptr && wParam != SIZE_MINIMIZED )
                {
                    pState->m_resizeWidth = LOWORD( lParam );
                    pState->m_resizeHeight = HIWORD( lParam );
                }
                return 0;
            }
            break;

            case WM_NCHITTEST:
            {
                // Mini timer: its grip strip drags the borderless window
                POINT pt = { static_cast<short>( LOWORD( lParam ) ), static_cast<short>( HIWORD( lParam ) ) };
                ScreenToClient( hWnd, &pt );
                if( pState != nullptr && pState->m_isMini && pt.x >= 0 && pt.x < static_cast<LONG>( g_miniGripWidth * DpiScale( hWnd ) ) )
                {
                    return HTCAPTION;
                }
            }
            break;

            case WM_CONTEXTMENU:
            {
                // Mini timer: right-click menu to close it without the tray
                if( pState == nullptr || !pState->m_isMini )
                {
                    break;
                }

                POINT pt = { static_cast<short>( LOWORD( lParam ) ), static_cast<short>( HIWORD( lParam ) ) };
                if( lParam == -1 )
                {
                    GetCursorPos( &pt );
                }

                HMENU hMenu = CreatePopupMenu();
                AppendMenuW( hMenu, MF_STRING, 1, L"Close mini timer" );
                UINT const command = TrackPopupMenu( hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, nullptr );
                DestroyMenu( hMenu );
                if( command == 1 )
                {
                    DestroyWindow( hWnd );
                }
                return 0;
            }
            break;

            case WM_GETMINMAXINFO:
            {
                if( pState != nullptr && pState->m_isMini )
                {
                    return 0;
                }

                float const scale = DpiScale( hWnd );
                MINMAXINFO* pInfo = reinterpret_cast<MINMAXINFO*>( lParam );
                pInfo->ptMinTrackSize.x = static_cast<LONG>( g_minWidth * scale );
                pInfo->ptMinTrackSize.y = static_cast<LONG>( g_minHeight * scale );
                return 0;
            }
            break;

            case WM_DPICHANGED:
            {
                if( ImGui::GetCurrentContext() != nullptr )
                {
                    UI::ApplyStyle( static_cast<float>( HIWORD( wParam ) ) / USER_DEFAULT_SCREEN_DPI );
                }

                RECT const* pSuggested = reinterpret_cast<RECT const*>( lParam );
                SetWindowPos( hWnd, nullptr, pSuggested->left, pSuggested->top, pSuggested->right - pSuggested->left, pSuggested->bottom - pSuggested->top, SWP_NOZORDER | SWP_NOACTIVATE );
                return 0;
            }
            break;

            case WM_SYSCOMMAND:
            {
                // No Alt menu
                if( ( wParam & 0xfff0 ) == SC_KEYMENU )
                {
                    return 0;
                }
            }
            break;

            case Platform::g_openSettingsMessage:
            {
                if( pState != nullptr )
                {
                    pState->m_openSettings = true;
                }
                return 0;
            }
            break;

            case WM_DESTROY:
            {
                if( pState != nullptr && !pState->m_isScreenshot && !pState->m_isMini )
                {
                    SavePlacement( hWnd, *pState->m_pStore );
                }
                PostQuitMessage( 0 );
                return 0;
            }
            break;
        }
        return DefWindowProcW( hWnd, msg, wParam, lParam );
    }

    // Saves the back buffer (RGBA) as a PNG through WIC
    static bool SaveBackBuffer( Graphics const& graphics, std::filesystem::path const& path )
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
        graphics.m_swapChain->GetBuffer( 0, IID_PPV_ARGS( &backBuffer ) );
        D3D11_TEXTURE2D_DESC desc;
        backBuffer->GetDesc( &desc );
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        if( FAILED( graphics.m_device->CreateTexture2D( &desc, nullptr, &staging ) ) )
        {
            return false;
        }
        graphics.m_context->CopyResource( staging.Get(), backBuffer.Get() );

        D3D11_MAPPED_SUBRESOURCE mapped;
        if( FAILED( graphics.m_context->Map( staging.Get(), 0, D3D11_MAP_READ, 0, &mapped ) ) )
        {
            return false;
        }

        // RGBA to opaque BGRA, which every WIC PNG encoder takes
        UINT const stride = desc.Width * 4;
        std::vector<uint8_t> pixels( static_cast<size_t>( stride ) * desc.Height );
        for( UINT y = 0; y < desc.Height; y++ )
        {
            uint8_t const* pRow = static_cast<uint8_t const*>( mapped.pData ) + static_cast<size_t>( y ) * mapped.RowPitch;
            uint8_t* pOut = pixels.data() + static_cast<size_t>( y ) * stride;
            for( UINT x = 0; x < desc.Width; x++ )
            {
                pOut[x * 4 + 0] = pRow[x * 4 + 2];
                pOut[x * 4 + 1] = pRow[x * 4 + 1];
                pOut[x * 4 + 2] = pRow[x * 4 + 0];
                pOut[x * 4 + 3] = 255;
            }
        }
        graphics.m_context->Unmap( staging.Get(), 0 );

        std::error_code ec;
        std::filesystem::create_directories( path.parent_path(), ec );

        Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
        Microsoft::WRL::ComPtr<IWICStream> stream;
        Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
        Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        bool const ok = SUCCEEDED( CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &factory ) ) )
            && SUCCEEDED( factory->CreateStream( &stream ) )
            && SUCCEEDED( stream->InitializeFromFilename( path.c_str(), GENERIC_WRITE ) )
            && SUCCEEDED( factory->CreateEncoder( GUID_ContainerFormatPng, nullptr, &encoder ) )
            && SUCCEEDED( encoder->Initialize( stream.Get(), WICBitmapEncoderNoCache ) )
            && SUCCEEDED( encoder->CreateNewFrame( &frame, nullptr ) )
            && SUCCEEDED( frame->Initialize( nullptr ) )
            && SUCCEEDED( frame->SetSize( desc.Width, desc.Height ) )
            && SUCCEEDED( frame->SetPixelFormat( &format ) )
            && SUCCEEDED( frame->WritePixels( desc.Height, stride, static_cast<UINT>( pixels.size() ), pixels.data() ) )
            && SUCCEEDED( frame->Commit() )
            && SUCCEEDED( encoder->Commit() );
        return ok;
    }

    // --screenshot uses a throwaway folder with sample data, so the picture shows a lived-in app
    static void SeedDemo( Core::Store const& store )
    {
        auto const rule = []( uint64_t id, char const* pKeyword, Core::Mode mode, char const* pDescription, char const* pProject )
        {
            return Core::Rule{ id, pKeyword, mode, pDescription, pProject, true };
        };

        Core::Settings settings;
        settings.m_rules =
        {
            rule( 1, "Code", Core::Mode::Phrase, "Programming", "Website" ),
            rule( 2, "figma", Core::Mode::Phrase, "Design work", "Website" ),
            rule( 3, "outlook", Core::Mode::Phrase, "Email", "Admin" ),
            rule( 4, "excel budget", Core::Mode::AllWords, "Budget planning", "Finance" ),
        };
        settings.m_firstRunDone = true;
        store.SaveSettings( settings );

        int64_t const t = Core::Now();
        auto const entry = [&]( int64_t ago, int64_t length, char const* pDescription, char const* pProject, bool isAuto )
        {
            store.AppendEntry( Core::Entry{ t - ago, t - ago + length, pDescription, pProject, isAuto } );
        };

        entry( 3 * 86400 + 600, 5400, "Budget planning", "Finance", true );
        entry( 86400 + 4000, 2700, "Email", "Admin", true );
        entry( 86400 + 9000, 4800, "Programming", "Website", true );
        entry( 86400 + 14000, 1800, "Team meeting", "", false );
        entry( 7200, 1500, "Email", "Admin", true );
        entry( 12600, 3900, "Design work", "Website", true );
        entry( 17000, 2400, "Programming", "Website", true );

        // App-switch fragments, shown grouped
        for( int64_t k = 0; k < 6; k++ )
        {
            entry( 5000 + k * 200, 90, "Browsing", "Research", true );
        }

        // Interleaved app-switching
        char const* const activities[3][2] = { { "Browsing", "Research" }, { "Clip Studio", "Art" }, { "Claude", "AI" } };
        for( int64_t k = 0; k < 10; k++ )
        {
            entry( 2 * 86400 + 3000 - k * 240, 100 + k % 4 * 40, activities[k % 3][0], activities[k % 3][1], true );
        }

        // Older days, so the timeline has history
        char const* const older[4][2] = { { "Programming", "Website" }, { "Budget planning", "Finance" }, { "Design work", "Website" }, { "Email", "Admin" } };
        for( int64_t day = 4; day < 13; day++ )
        {
            entry( day * 86400 + 20000, 3600 + day % 3 * 1800, older[day / 3 % 4][0], older[day / 3 % 4][1], true );
            entry( day * 86400 + 9000, 2400, older[3][0], older[3][1], true );
        }

        store.SaveTimer( Core::Timer{ t - 1534, "Programming", "Website", 1 } );
        store.SaveStatus( Core::Status{ t, "Code", "main.rs - website" } );
        store.SaveRecent( { "Discord", "Photoshop", "Teams" } );
    }

    // The installer rewrites the Run key on every update; put back the user's choice (on by default)
    static void ApplyAutostart( Core::Store const& store )
    {
        bool const on = store.LoadSettings().m_autostart;
        if( on != Platform::IsAutostartEnabled() )
        {
            Platform::SetAutostart( on );
        }
    }

    //-------------------------------------------------------------------------

    static int Run( std::vector<std::wstring> const& args )
    {
        auto const hasArg = [&args]( wchar_t const* pName ) { return std::ranges::find( args, pName ) != args.end(); };
        auto const argValue = [&args]( wchar_t const* pName ) -> std::optional<std::wstring>
        {
            auto const found = std::ranges::find( args, pName );
            if( found == args.end() || found + 1 == args.end() )
            {
                return std::nullopt;
            }
            return *( found + 1 );
        };

        SetProcessDpiAwarenessContext( DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
        if( hasArg( L"--tracker" ) )
        {
            Core::Store const store( Core::Store::DefaultDir() );
            ApplyAutostart( store );
            return Tracker::Run( store );
        }

        std::optional<std::wstring> const screenshot = argValue( L"--screenshot" );
        bool const isScreenshot = screenshot.has_value();
        bool const isMini = hasArg( L"--mini" );
        wchar_t const* const pWindowClass = isMini ? Platform::g_miniWindowClass : Platform::g_mainWindowClass;
        std::filesystem::path dir = Core::Store::DefaultDir();
        if( isScreenshot )
        {
            dir = std::filesystem::temp_directory_path() / std::format( "kui-shot-{}", GetCurrentProcessId() );
            std::error_code ec;
            std::filesystem::remove_all( dir, ec );
        }

        Core::Store store( dir );
        if( isScreenshot )
        {
            SeedDemo( store );
        }
        else
        {
            // One window (and one mini timer): a second launch just brings the first forward
            if( isMini ? Platform::IsMiniTimerOpen() : Platform::FocusMainWindow() )
            {
                return 0;
            }

            ApplyAutostart( store );

            if( !Tracker::IsAlive( store.LoadStatus() ) )
            {
                Platform::SpawnTracker();
            }
        }

        UI::StartOptions options;
        options.m_isScreenshot = isScreenshot;
        options.m_isMini = isMini;
        options.m_view = hasArg( L"--programs" ) ? UI::View::Programs : options.m_view;
        options.m_view = hasArg( L"--calendar" ) ? UI::View::Calendar : options.m_view;
        options.m_view = hasArg( L"--timeline" ) ? UI::View::Timeline : options.m_view;
        options.m_showSettings = hasArg( L"--settings" );
        options.m_view = ( hasArg( L"--reports" ) || hasArg( L"--workload" ) ) ? UI::View::Reports : options.m_view;
        options.m_reportTab = hasArg( L"--workload" ) ? 1 : 0;
        options.m_showSetup = hasArg( L"--setup" ) || hasArg( L"--setup2" );
        options.m_setupStep = hasArg( L"--setup2" ) ? 1 : 0;

        // Window
        HINSTANCE const hInstance = GetModuleHandleW( nullptr );
        WNDCLASSEXW wc = { sizeof( wc ) };
        wc.style = CS_CLASSDC;
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hInstance;
        wc.hIcon = LoadIconW( hInstance, MAKEINTRESOURCEW( 1 ) );
        wc.hCursor = LoadCursorW( nullptr, IDC_ARROW );
        wc.lpszClassName = pWindowClass;
        RegisterClassExW( &wc );

        // --scale simulates Windows display scaling (screenshots default to 100%)
        float scale = static_cast<float>( GetDpiForSystem() ) / USER_DEFAULT_SCREEN_DPI;
        if( isScreenshot )
        {
            std::optional<std::wstring> const scaleArg = argValue( L"--scale" );
            scale = scaleArg.has_value() ? std::stof( *scaleArg ) : 1.0f;
        }

        float const width = ( isMini ? g_miniWidth : g_windowWidth ) * scale;
        float const height = ( isMini ? g_miniHeight : g_windowHeight ) * scale;
        HWND hWnd = nullptr;
        if( isMini )
        {
            // Borderless, always on top, no taskbar button; starts by the tray
            Platform::UseDarkMenus();
            RECT work;
            SystemParametersInfoW( SPI_GETWORKAREA, 0, &work, 0 );
            int const margin = static_cast<int>( 16 * scale );
            hWnd = CreateWindowExW( WS_EX_TOPMOST | WS_EX_TOOLWINDOW, pWindowClass, L"Kui mini timer", WS_POPUP, work.right - static_cast<int>( width ) - margin, work.bottom - static_cast<int>( height ) - margin, static_cast<int>( width ), static_cast<int>( height ), nullptr, nullptr, hInstance, nullptr );
            DWM_WINDOW_CORNER_PREFERENCE const corners = DWMWCP_ROUND;
            DwmSetWindowAttribute( hWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof( corners ) );
        }
        else
        {
            RECT rect = { 0, 0, static_cast<LONG>( width ), static_cast<LONG>( height ) };
            AdjustWindowRectExForDpi( &rect, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem() );
            hWnd = CreateWindowExW( 0, pWindowClass, L"Kui", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, hInstance, nullptr );
        }

        // Dark title bar in the app's colour, icon and buttons only (caption colour needs Windows 11)
        BOOL const dark = TRUE;
        DwmSetWindowAttribute( hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof( dark ) );
        ImVec4 const side = ImGui::ColorConvertU32ToFloat4( UI::g_side );
        COLORREF const caption = RGB( side.x * 255.0f, side.y * 255.0f, side.z * 255.0f );
        DwmSetWindowAttribute( hWnd, DWMWA_CAPTION_COLOR, &caption, sizeof( caption ) );
        DwmSetWindowAttribute( hWnd, DWMWA_BORDER_COLOR, &caption, sizeof( caption ) );
        WTA_OPTIONS const noCaption = { WTNCA_NODRAWCAPTION, WTNCA_NODRAWCAPTION };
        SetWindowThemeAttribute( hWnd, WTA_NONCLIENT, const_cast<WTA_OPTIONS*>( &noCaption ), sizeof( noCaption ) );
        if( isScreenshot )
        {
            // Hidden window: size the client area exactly
            SetWindowPos( hWnd, nullptr, 0, 0, static_cast<int>( width ), static_cast<int>( height ), SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE );
            RECT client;
            GetClientRect( hWnd, &client );
            int const dx = static_cast<int>( width ) - client.right;
            int const dy = static_cast<int>( height ) - client.bottom;
            SetWindowPos( hWnd, nullptr, 0, 0, static_cast<int>( width ) + dx, static_cast<int>( height ) + dy, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOMOVE );
        }

        WindowState state;
        state.m_pStore = &store;
        state.m_isScreenshot = isScreenshot;
        state.m_isMini = isMini;
        SetWindowLongPtrW( hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( &state ) );

        Graphics graphics;
        if( !CreateGraphics( hWnd, graphics ) )
        {
            MessageBoxW( hWnd, L"Couldn't start Direct3D 11.", L"Kui", MB_ICONERROR );
            return 1;
        }

        if( !isScreenshot )
        {
            ShowWindow( hWnd, isMini ? SW_SHOWNORMAL : RestorePlacement( hWnd, store ) );
            UpdateWindow( hWnd );
            scale = DpiScale( hWnd );   // The restored position may be on another monitor
        }

        // ImGui
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        UI::LoadFonts();
        UI::ApplyStyle( scale );
        ImGui_ImplWin32_Init( hWnd );
        ImGui_ImplDX11_Init( graphics.m_device.Get(), graphics.m_context.Get() );

        UI::App app( store, options );
        int busyFrames = g_busyFrames;
        int frames = 0;
        bool done = false;
        int result = 0;
        while( !done )
        {
            // Idle: wake once a second for the clock, or on input
            if( busyFrames <= 0 && !isScreenshot )
            {
                MsgWaitForMultipleObjects( 0, nullptr, FALSE, 1000, QS_ALLINPUT );
            }

            MSG msg;
            bool hadInput = false;
            while( PeekMessageW( &msg, nullptr, 0, 0, PM_REMOVE ) )
            {
                hadInput = true;
                TranslateMessage( &msg );
                DispatchMessageW( &msg );
                done |= msg.message == WM_QUIT;
            }

            if( done )
            {
                break;
            }
            busyFrames = hadInput ? g_busyFrames : busyFrames - 1;

            if( state.m_resizeWidth != 0 && state.m_resizeHeight != 0 )
            {
                graphics.m_renderTarget.Reset();
                graphics.m_swapChain->ResizeBuffers( 0, state.m_resizeWidth, state.m_resizeHeight, DXGI_FORMAT_UNKNOWN, 0 );
                state.m_resizeWidth = 0;
                state.m_resizeHeight = 0;
                CreateRenderTarget( graphics );
            }

            if( std::exchange( state.m_openSettings, false ) )
            {
                app.OpenSettings();
            }

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            app.Draw();
            ImGui::Render();

            ImVec4 const clear = ImGui::ColorConvertU32ToFloat4( UI::g_background );
            float const clearColor[4] = { clear.x, clear.y, clear.z, 1.0f };
            ID3D11RenderTargetView* const pTarget = graphics.m_renderTarget.Get();
            graphics.m_context->OMSetRenderTargets( 1, &pTarget, nullptr );
            graphics.m_context->ClearRenderTargetView( pTarget, clearColor );
            ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData() );

            if( isScreenshot && ++frames == g_screenshotFrames )
            {
                result = SaveBackBuffer( graphics, *screenshot ) ? 0 : 1;
                break;
            }
            graphics.m_swapChain->Present( 1, 0 );
        }

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        DestroyWindow( hWnd );
        UnregisterClassW( pWindowClass, hInstance );
        return result;
    }
}

int WINAPI wWinMain( HINSTANCE, HINSTANCE, PWSTR, int )
{
    CoInitializeEx( nullptr, COINIT_APARTMENTTHREADED );
    int argc = 0;
    LPWSTR* ppArgv = CommandLineToArgvW( GetCommandLineW(), &argc );
    std::vector<std::wstring> args( ppArgv, ppArgv + argc );
    LocalFree( ppArgv );
    return Kui::Run( args );
}
