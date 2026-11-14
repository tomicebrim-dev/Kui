#include "UI/App.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <thread>

#include "Core/Layout.h"
#include "Core/Text.h"
#include "Platform/Platform.h"
#include "UI/Widgets.h"

//-------------------------------------------------------------------------
// State, actions and the frame layout. The views are in the App_*.cpp files.
//-------------------------------------------------------------------------

namespace Kui::UI
{
    // Toasts and undo stay this many seconds
    constexpr double g_toastSecs = 10.0;

    //-------------------------------------------------------------------------

    App::App( Core::Store store, StartOptions const& options )
        : m_store( std::move( store ) )
        , m_isScreenshot( options.m_isScreenshot )
        , m_view( options.m_view )
        , m_reportTab( options.m_reportTab )
    {
        m_settings = m_store.LoadSettings();
        m_settings.m_textScale = std::clamp( m_settings.m_textScale, g_minTextScale, g_maxTextScale );
        ImGui::GetStyle().FontScaleMain = m_settings.m_textScale;
        if( m_settings.m_checkUpdates && !m_isScreenshot )
        {
            CheckForUpdate();
        }
        if( !m_settings.m_firstRunDone || options.m_showSetup )
        {
            m_setup = SetupState{ options.m_setupStep, Setup::Detect() };
        }
        Reload( true );
    }

    void App::Reload( bool force )
    {
        m_timer = m_store.LoadTimer();
        m_status = m_store.LoadStatus();
        m_recent = m_store.LoadRecent();
        std::optional<std::filesystem::file_time_type> const stamp = m_store.Modified( "entries.jsonl" );
        if( !force && stamp == m_entriesStamp )
        {
            return;
        }

        m_entriesStamp = stamp;
        m_entries = m_store.LoadEntries();
        std::ranges::stable_sort( m_entries, std::greater<>(), &Core::Entry::m_start );

        m_daySecs.clear();
        m_longestEntry = 0;
        m_groups.clear();
        m_activities.clear();
        m_projects.clear();

        std::map<std::pair<std::string, std::string>, size_t> dayGroups;
        std::set<std::pair<std::string, std::string>> seenActivities;
        std::set<std::string> seenProjects;
        std::optional<Core::Day> currentDay;
        for( size_t i = 0; i < m_entries.size(); i++ )
        {
            Core::Entry const& entry = m_entries[i];
            Core::Day const day = Core::DayOf( entry.m_start );
            int64_t const secs = entry.m_end - entry.m_start;
            m_daySecs[day] += secs;
            m_longestEntry = std::max( m_longestEntry, secs );
            if( currentDay != day )
            {
                currentDay = day;
                dayGroups.clear();
            }

            std::pair<std::string, std::string> const key( entry.m_description, entry.m_project );
            auto const found = dayGroups.find( key );
            if( found != dayGroups.end() )
            {
                Group& group = m_groups[found->second];
                group.m_items.push_back( i );
                group.m_total += secs;
                group.m_first = std::min( group.m_first, entry.m_start );
                group.m_isAuto = group.m_isAuto && entry.m_isAuto;
            }
            else
            {
                dayGroups.emplace( key, m_groups.size() );
                m_groups.push_back( Group{ GroupKey{ day, entry.m_description, entry.m_project }, entry.m_isAuto, { i }, secs, entry.m_start, entry.m_end } );
            }

            if( m_activities.size() < 300 && !entry.m_description.empty() && seenActivities.insert( key ).second )
            {
                m_activities.push_back( Activity{ entry.m_description, entry.m_project } );
            }

            if( !entry.m_project.empty() && seenProjects.insert( entry.m_project ).second )
            {
                m_projects.push_back( entry.m_project );
            }
        }

        for( Core::Rule const& rule : m_settings.m_rules )
        {
            if( !rule.m_project.empty() && seenProjects.insert( rule.m_project ).second )
            {
                m_projects.push_back( rule.m_project );
            }
        }

        // Renamed or deleted groups leave stale keys behind
        std::erase_if( m_expanded, [this]( GroupKey const& key ) { return std::ranges::none_of( m_groups, [&key]( Group const& g ) { return g.m_key == key; } ); } );
    }

    void App::Save() const
    {
        if( !m_isScreenshot )
        {
            m_store.SaveSettings( m_settings );
        }
    }

    void App::Start( std::string description, std::string project )
    {
        m_store.StartTimer( Core::Timer{ Core::Now(), std::move( description ), std::move( project ), std::nullopt } );
        Reload( true );
    }

    void App::Stop()
    {
        m_store.StopTimer( Core::Now() );
        Reload( true );
    }

    void App::Toggle()
    {
        if( m_timer.has_value() )
        {
            Stop();
            return;
        }

        Start( std::exchange( m_description, {} ), std::exchange( m_project, {} ) );
    }

    void App::Delete( std::vector<Core::Entry> entries )
    {
        m_store.RewriteEntries( entries, {}, "", "" );
        ShowToast( entries.size() == 1 ? std::string( "Entry deleted" ) : std::format( "{} entries deleted", entries.size() ) );
        m_undo = Undo{ std::move( entries ), m_toast->m_time };
        Reload( true );
    }

    void App::UndoDelete()
    {
        if( !m_undo.has_value() )
        {
            return;
        }

        for( Core::Entry const& entry : m_undo->m_entries )
        {
            m_store.AppendEntry( entry );
        }
        m_undo.reset();
        m_toast.reset();
        Reload( true );
    }

    void App::ShowToast( std::string message )
    {
        m_toast = Toast{ std::move( message ), ImGui::GetTime() };
    }

    // Like std::async, but the future doesn't block on destruction: closing the window never waits on the network
    template<typename F>
    static auto RunDetached( F f )
    {
        std::packaged_task<std::invoke_result_t<F>()> task( std::move( f ) );
        auto future = task.get_future();
        std::thread( std::move( task ) ).detach();
        return future;
    }

    void App::CheckForUpdate()
    {
        if( m_updateCheck.valid() || m_updateInstall.valid() )
        {
            return;
        }

        m_updateStatus = "Checking for updates…";
        m_updateCheck = RunDetached( [] { return Updater::FetchLatest(); } );
    }

    void App::InstallUpdate()
    {
        if( !m_update.has_value() || m_updateInstall.valid() )
        {
            return;
        }

        m_updateStatus = std::format( "Downloading Kui {}…", m_update->m_version );
        m_updateInstall = RunDetached( [release = *m_update] { return Updater::Install( release ); } );
    }

    void App::PollUpdate()
    {
        auto const isReady = []( auto const& future ) { return future.valid() && future.wait_for( std::chrono::seconds( 0 ) ) == std::future_status::ready; };
        if( isReady( m_updateCheck ) )
        {
            std::optional<Updater::Release> const latest = m_updateCheck.get();
            bool const isNewer = latest.has_value() && !latest->m_msiUrl.empty() && Updater::IsNewer( latest->m_version, Updater::CurrentVersion() );
            m_update = isNewer ? latest : std::nullopt;
            m_updateStatus = !latest.has_value() ? "Couldn't reach GitHub" : isNewer ? std::format( "Kui {} is available", latest->m_version ) : "Kui is up to date";
        }

        if( isReady( m_updateInstall ) )
        {
            if( m_updateInstall.get() )
            {
                // The installer replaces kui.exe and reopens it
                PostQuitMessage( 0 );
                return;
            }

            m_updateStatus = "Couldn't download the update";
            ShowToast( m_updateStatus );
        }
    }

    void App::AddRule( std::string const& keyword, std::string const& description, std::string const& project )
    {
        std::string const k = Core::Trim( keyword );
        if( k.empty() )
        {
            return;
        }

        uint64_t id = 0;
        for( Core::Rule const& rule : m_settings.m_rules )
        {
            if( Core::IEqualsAscii( Core::Trim( rule.m_keyword ), k ) )
            {
                return;
            }
            id = std::max( id, rule.m_id );
        }

        Core::Rule rule;
        rule.m_id = id + 1;
        rule.m_keyword = k;
        rule.m_description = description;
        rule.m_project = project;
        m_settings.m_rules.push_back( rule );
    }

    int64_t App::LiveSecs() const
    {
        return m_timer.has_value() ? Core::Now() - m_timer->m_start : 0;
    }

    ProjectSecs App::DayParts( Core::Day day ) const
    {
        int64_t const from = Core::DayStart( day );
        int64_t const to = Core::DayStart( day + 1 );
        ProjectSecs parts;
        auto add = [&]( int64_t start, int64_t end, std::string const& project )
        {
            int64_t const secs = std::min( end, to ) - std::max( start, from );
            if( secs <= 0 )
            {
                return;
            }

            auto const found = std::ranges::find( parts, project, &ProjectSecs::value_type::first );
            if( found != parts.end() )
            {
                found->second += secs;
            }
            else
            {
                parts.emplace_back( project, secs );
            }
        };

        for( Core::Entry const* pEntry : Core::Span( m_entries, from, to, m_longestEntry ) )
        {
            add( pEntry->m_start, pEntry->m_end, pEntry->m_project );
        }

        if( m_timer.has_value() )
        {
            add( m_timer->m_start, Core::Now(), m_timer->m_project );
        }

        std::ranges::stable_sort( parts, std::greater<>(), &ProjectSecs::value_type::second );
        return parts;
    }

    //-------------------------------------------------------------------------

    void App::HandleShortcuts()
    {
        for( int i = 0; i < 5; i++ )
        {
            if( ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | static_cast<ImGuiKey>( ImGuiKey_1 + i ) ) )
            {
                m_view = static_cast<View>( i );
            }
        }

        bool const undo = ImGui::IsKeyChordPressed( ImGuiMod_Ctrl | ImGuiKey_Z );
        bool toggle = false;
        int nav = 0;
        if( !ImGui::GetIO().WantTextInput )
        {
            toggle = ImGui::IsKeyChordPressed( ImGuiKey_S );
            m_focusDescription |= ImGui::IsKeyChordPressed( ImGuiKey_N );
            nav = ImGui::IsKeyChordPressed( ImGuiKey_LeftArrow ) ? -1 : nav;
            nav = ImGui::IsKeyChordPressed( ImGuiKey_RightArrow ) ? 1 : nav;
            nav = ImGui::IsKeyChordPressed( ImGuiKey_T ) ? 2 : nav;
        }

        if( m_view == View::Calendar || m_view == View::Reports )
        {
            m_week = ( nav == 2 ) ? 0 : m_week + nav;
        }

        if( toggle )
        {
            Toggle();
        }

        if( undo )
        {
            UndoDelete();
        }

        if( m_undo.has_value() && ImGui::GetTime() - m_undo->m_time > g_toastSecs )
        {
            m_undo.reset();
        }
    }

    void App::Draw()
    {
        ImGuiViewport const* pViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos( pViewport->WorkPos );
        ImGui::SetNextWindowSize( pViewport->WorkSize );
        ImGuiWindowFlags const flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::Begin( "Kui", nullptr, flags );

        double const time = ImGui::GetTime();
        if( time - m_checkedAt > 1.0 )
        {
            Reload( false );
            m_checkedAt = time;
            if( m_view == View::Programs )
            {
                m_openPrograms = m_isScreenshot ? std::vector<std::string>{ "chrome", "Slack", "Spotify", "Code", "Notepad" } : Platform::OpenPrograms();
            }
        }

        PollUpdate();
        if( m_setup.has_value() )
        {
            DrawSetup();
            ImGui::End();
            return;
        }

        HandleShortcuts();
        DrawSidebar();
        ImGui::SameLine( 0, 0 );

        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( Px( 24 ), Px( 18 ) ) );
        ImGui::BeginChild( "main", ImVec2( 0, 0 ), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
        ImGui::PopStyleVar();

        DrawTopBar();
        ImGui::Dummy( ImVec2( 0, Px( 2 ) ) );
        ImGui::BeginChild( "view", ImVec2( 0, 0 ), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
        switch( m_view )
        {
            case View::Timer:
            {
                DrawEntries();
            }
            break;

            case View::Calendar:
            {
                DrawCalendar();
            }
            break;

            case View::Reports:
            {
                DrawReports();
            }
            break;

            case View::Programs:
            {
                DrawPrograms();
            }
            break;

            case View::Settings:
            {
                DrawSettings();
            }
            break;
        }
        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::End();

        DrawToast();
    }

    void App::DrawToast()
    {
        if( !m_toast.has_value() )
        {
            return;
        }

        if( ImGui::GetTime() - m_toast->m_time > g_toastSecs )
        {
            m_toast.reset();
            return;
        }

        bool const canUndo = m_undo.has_value() && m_undo->m_time == m_toast->m_time;
        ImGuiViewport const* pViewport = ImGui::GetMainViewport();
        ImVec2 const anchor( pViewport->WorkPos.x + pViewport->WorkSize.x / 2, pViewport->WorkPos.y + pViewport->WorkSize.y - Px( 20 ) );
        ImGui::SetNextWindowPos( anchor, ImGuiCond_Always, ImVec2( 0.5f, 1.0f ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( Px( 16 ), Px( 8 ) ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 1.0f );
        ImGui::PushStyleColor( ImGuiCol_WindowBg, IM_COL32( 44, 51, 60, 255 ) );
        ImGuiWindowFlags const flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
        ImGui::Begin( "##toast", nullptr, flags );
        ImGui::PopStyleColor();
        ImGui::PopStyleVar( 2 );

        ImGui::AlignTextToFramePadding();
        Label( m_toast->m_message );
        bool undo = false;
        if( canUndo )
        {
            ImGui::SameLine( 0, Px( 14 ) );
            undo = FlatButton( "undo", "Undo", 14.0f, g_accent, true );
            ImGui::SetItemTooltip( "Ctrl+Z" );
        }

        ImGui::SameLine( 0, Px( 14 ) );
        bool const close = FlatButton( "close", "✕", 14.0f );
        ImGui::End();

        if( undo )
        {
            UndoDelete();
        }
        else if( close )
        {
            m_toast.reset();
        }
    }
}
