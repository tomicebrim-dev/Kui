#include "UI/App.h"

#include <imgui.h>

#include <algorithm>
#include <format>

#include "Platform/Platform.h"
#include "Tracker/Tracker.h"
#include "UI/Widgets.h"

//-------------------------------------------------------------------------
// Far-left icon rail (logo, page links, Settings), then the sidebar: today's totals and the tracker status.
// The sidebar footer is pinned; the rest scrolls.
//-------------------------------------------------------------------------

namespace Kui::UI
{
    void App::DrawRail()
    {
        float const width = Px( 60 );
        ImGui::PushStyleColor( ImGuiCol_ChildBg, g_field );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( Px( 10 ), Px( 16 ) ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, 0.0f );   // Flat edge panels
        ImGui::BeginChild( "rail", ImVec2( width, 0 ), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
        ImGui::PopStyleVar( 2 );
        ImGui::PopStyleColor();

        // Logo: a clock face, on the same row as the sidebar's name
        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        ImVec2 const pos = ImGui::GetCursorScreenPos();
        ImVec2 const center( pos.x + Px( 20 ), pos.y + Px( 16 ) );
        pDrawList->AddCircleFilled( center, Px( 16 ), g_accent );
        pDrawList->AddCircle( center, Px( 7.5f ), g_field, 0, Px( 2.5f ) );
        pDrawList->AddLine( ImVec2( center.x, center.y - Px( 10 ) ), ImVec2( center.x, center.y - Px( 3 ) ), g_field, Px( 2.5f ) );
        ImGui::Dummy( ImVec2( Px( 40 ), Px( 32 ) ) );
        ImGui::Dummy( ImVec2( 0, Px( 10 ) ) );

        struct RailLink
        {
            View            m_view;
            Icon            m_icon;
            char const*     m_pId;
            char const*     m_pTooltip;
        };

        constexpr RailLink links[] =
        {
            { View::Timer, Icon::Timer, "timer", "Timer  Ctrl+1" },
            { View::Calendar, Icon::Calendar, "calendar", "Calendar  Ctrl+2" },
            { View::Timeline, Icon::Timeline, "timeline", "Timeline  Ctrl+3" },
            { View::Reports, Icon::Reports, "reports", "Reports  Ctrl+4" },
            { View::Programs, Icon::Programs, "programs", "Programs  Ctrl+5" },
        };

        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( Px( 8 ), Px( 6 ) ) );
        for( RailLink const& link : links )
        {
            if( RailItem( link.m_pId, link.m_icon, link.m_pTooltip, m_view == link.m_view ) )
            {
                m_view = link.m_view;
            }
        }
        ImGui::PopStyleVar();

        // Settings pinned to the bottom; opens the settings popup
        ImGui::SetCursorPosY( ImGui::GetWindowHeight() - Px( 16 ) - Px( 40 ) );
        m_openSettings |= RailItem( "settings", Icon::Settings, "Settings  Ctrl+6", m_settingsOpen );
        ImGui::EndChild();
    }

    void App::DrawSidebar()
    {
        float const width = std::clamp( ImGui::GetContentRegionAvail().x * 0.25f, Px( 200 ), Px( 250 ) );
        ImGui::PushStyleColor( ImGuiCol_ChildBg, g_side );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( Px( 16 ), Px( 16 ) ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, 0.0f );   // Flat edge panels
        ImGui::BeginChild( "sidebar", ImVec2( width, 0 ), ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
        ImGui::PopStyleVar( 2 );
        ImGui::PopStyleColor();

        // Footer height is measured the frame before, so the body leaves room for it
        ImGui::BeginChild( "sideBody", ImVec2( 0, -( m_sideFooterHeight + Px( 10 ) ) ) );
        DrawSidebarBody();
        ImGui::EndChild();

        ImGui::Dummy( ImVec2( 0, Px( 2 ) ) );
        float const footerTop = ImGui::GetCursorPosY();
        DrawSidebarFooter();
        m_sideFooterHeight = ImGui::GetCursorPosY() - footerTop;
        ImGui::EndChild();
    }

    void App::DrawSidebarBody()
    {
        // Name, beside the rail's logo
        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        ImVec2 const pos = ImGui::GetCursorScreenPos();
        TextAt( pDrawList, ImVec2( pos.x + Px( 4 ), pos.y + Px( 16 ) ), "Kui", 17.0f, g_text, true );
        ImGui::Dummy( ImVec2( Px( 32 ), Px( 32 ) ) );
        ImGui::Dummy( ImVec2( 0, Px( 10 ) ) );

        Core::Day const today = Core::DayOf( Core::Now() );
        int64_t const live = LiveSecs();
        auto const daySecs = [this]( Core::Day day )
        {
            auto const found = m_daySecs.find( day );
            return found != m_daySecs.end() ? found->second : 0;
        };

        int64_t weekTotal = live;
        for( int i = 0; i < 7; i++ )
        {
            weekTotal += daySecs( today - i );
        }

        Caption( "TODAY" );
        Label( Core::FormatHMS( daySecs( today ) + live ), 28.0f, g_text, true );
        Caption( "LAST 7 DAYS" );
        Label( Core::FormatHMS( weekTotal ), 18.0f, g_muted );
        ImGui::Dummy( ImVec2( 0, Px( 6 ) ) );

        // Today's activities, the running one included
        struct Row
        {
            std::string     m_description;
            std::string     m_project;
            int64_t         m_secs = 0;
        };

        std::vector<Row> rows;
        for( Group const& group : m_groups )
        {
            if( group.m_key.m_day != today )
            {
                break;
            }
            rows.push_back( Row{ group.m_key.m_description, group.m_key.m_project, group.m_total } );
        }

        if( m_timer.has_value() )
        {
            auto const found = std::ranges::find_if( rows, [this]( Row const& r ) { return r.m_description == m_timer->m_description && r.m_project == m_timer->m_project; } );
            if( found != rows.end() )
            {
                found->m_secs += live;
            }
            else
            {
                rows.push_back( Row{ m_timer->m_description, m_timer->m_project, live } );
            }
        }
        std::ranges::stable_sort( rows, std::greater<>(), &Row::m_secs );

        Caption( "TODAY BY ACTIVITY" );
        if( rows.empty() )
        {
            Label( "Nothing tracked yet", g_bodySize, g_muted );
            return;
        }

        float const max = static_cast<float>( std::max<int64_t>( rows.front().m_secs, 1 ) );
        float const width = ImGui::GetContentRegionAvail().x;
        float const lineHeight = Px( 20 );
        for( size_t i = 0; i < std::min<size_t>( rows.size(), 4 ); i++ )
        {
            Row const& row = rows[i];
            ImVec2 const rowPos = ImGui::GetCursorScreenPos();
            float const centerY = rowPos.y + lineHeight / 2;
            std::string const time = Core::FormatHM( row.m_secs );
            float const timeWidth = TextWidth( time, 12.5f );
            std::string const name = row.m_description.empty() ? "(no description)" : row.m_description;
            TextAt( pDrawList, ImVec2( rowPos.x, centerY ), name, 13.5f, g_text, false, 0.0f, width - timeWidth - Px( 8 ) );
            TextAt( pDrawList, ImVec2( rowPos.x + width, centerY ), time, 12.5f, g_muted, false, 1.0f );

            float const barY = rowPos.y + lineHeight + Px( 3 );
            float const fill = std::max( width * static_cast<float>( row.m_secs ) / max, Px( 4 ) );
            pDrawList->AddRectFilled( ImVec2( rowPos.x, barY ), ImVec2( rowPos.x + width, barY + Px( 4 ) ), g_line, Px( 2 ) );
            pDrawList->AddRectFilled( ImVec2( rowPos.x, barY ), ImVec2( rowPos.x + fill, barY + Px( 4 ) ), ColorFor( row.m_project ), Px( 2 ) );
            ImGui::Dummy( ImVec2( width, lineHeight + Px( 9 ) ) );
        }
    }

    void App::DrawSidebarFooter()
    {
        if( m_update.has_value() )
        {
            std::string const label = m_updateInstall.valid() ? "Downloading update…" : std::format( "Update to {}", m_update->m_version );
            if( AccentButton( label.c_str(), ImVec2( ImGui::GetContentRegionAvail().x / Px( 1 ), 0 ), !m_updateInstall.valid() ) )
            {
                InstallUpdate();
            }
            ImGui::SetItemTooltip( "Download and install the new version, then reopen Kui" );
        }

        bool const alive = Tracker::IsAlive( m_status );
        BeginCard( "status", g_background, 12.0f, 10.0f, g_background );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( Px( 8 ), Px( 4 ) ) );

        ImU32 dot = g_muted;
        char const* pState = "Tracker stopped";
        if( alive )
        {
            dot = m_settings.m_tracking ? g_green : g_palette[2];
            pState = m_settings.m_tracking ? "Auto-tracking on" : "Auto-tracking paused";
        }

        Dot( dot );
        ImGui::SameLine();
        Label( pState, 13.5f, g_text, true );

        if( !alive )
        {
            if( ImGui::Button( "Start tracker" ) )
            {
                Platform::SpawnTracker();
            }
        }
        else if( m_status.has_value() && !m_status->m_app.empty() )
        {
            Label( Ellipsize( "In front: " + m_status->m_app, ImGui::GetContentRegionAvail().x, 12.5f ), 12.5f, g_muted );
        }

        if( alive && m_status.has_value() && m_status->m_safeMode )
        {
            Label( "Anti-cheat detected: light scan", 12.5f, g_muted );
            ImGui::SetItemTooltip( "Kui only reads the window in front while an anti-cheat is running" );
        }

        ImGui::PopStyleVar();
        EndCard();

        if( FlatButton( "folder", "Data folder" ) )
        {
            Platform::OpenFolder( m_store.GetDir() );
        }

        ImGui::SameLine( 0, Px( 14 ) );
        FlatButton( "keys", "Keys" );
        ImGui::SetItemTooltip( "Ctrl+1–5  switch page\nCtrl+6  settings\nN  type what you're doing\nS  start / stop timer\n← →  previous / next week\nT  back to this week\nCtrl+Z  undo delete / move\nEsc  cancel edit" );
    }
}
