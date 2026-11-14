#include "UI/App.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "Core/Text.h"
#include "Tracker/Tracker.h"
#include "UI/Widgets.h"

//-------------------------------------------------------------------------
// The timer card: what you're doing, project, clock, auto-tracking switch and start/stop. Suggestions below it.
//-------------------------------------------------------------------------

namespace Kui::UI
{
    void App::DrawTopBar()
    {
        BeginCard( "topBar", g_line, 14.0f, 14.0f );
        bool const running = m_timer.has_value();
        std::string& description = running ? m_timer->m_description : m_description;
        std::string& project = running ? m_timer->m_project : m_project;

        std::string subtitle = "Press Enter to start  ·  N to type, S to start/stop";
        if( running )
        {
            subtitle = ( m_timer->m_rule.has_value() ? "Auto-tracked since " : "Started " ) + Core::ClockHM( m_timer->m_start );
        }

        // One row: description, project, clock, watch button, play button
        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        ImVec2 const origin = ImGui::GetCursorScreenPos();
        float const avail = ImGui::GetContentRegionAvail().x;
        float const rowHeight = Px( 46 );
        float const gap = Px( 14 );
        float const projectWidth = std::clamp( avail * 0.24f, Px( 110 ), Px( 200 ) );
        float const descriptionWidth = std::max( avail - projectWidth - Px( 240 ), Px( 100 ) );
        bool changed = false;
        bool toggle = false;

        // Description: big frameless text box with the status line under it
        ImGui::SetCursorScreenPos( origin );
        if( std::exchange( m_focusDescription, false ) )
        {
            ImGui::SetKeyboardFocusHere();
        }

        ImGui::PushFont( GetSemiboldFont(), 21.0f );
        ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 0, 0, 0, 0 ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 0, 0 ) );
        ImGui::SetNextItemWidth( descriptionWidth );
        changed |= ImGui::InputTextWithHint( "##description", "What are you working on?", &description );
        ImGui::PopStyleVar( 2 );
        ImGui::PopStyleColor();
        ImGui::PopFont();
        bool const descriptionFocused = ImGui::IsItemActive();
        toggle |= !running && ImGui::IsItemDeactivated() && ( ImGui::IsKeyPressed( ImGuiKey_Enter ) || ImGui::IsKeyPressed( ImGuiKey_KeypadEnter ) );
        TextAt( pDrawList, ImVec2( origin.x, origin.y + Px( 37 ) ), subtitle, 12.5f, g_muted, false, 0.0f, descriptionWidth );

        float x = origin.x + descriptionWidth + gap;
        ImGui::SetCursorScreenPos( ImVec2( x, origin.y + Px( 8 ) ) );
        changed |= ProjectField( "project", project, projectWidth / Px( 1 ), m_projects );
        if( running && changed )
        {
            m_store.SaveTimer( *m_timer );
        }

        x += projectWidth + gap;
        std::string const clock = Core::FormatHMS( LiveSecs() );
        TextAt( pDrawList, ImVec2( x, origin.y + rowHeight / 2 ), clock, 26.0f, running ? g_text : g_muted, true );

        x += TextWidth( clock, 26.0f, true ) + gap;
        ImGui::SetCursorScreenPos( ImVec2( x, origin.y + Px( 9 ) ) );
        if( DrawWatchButton() )
        {
            m_settings.m_tracking = !m_settings.m_tracking;
            if( !m_settings.m_tracking && running && m_timer->m_rule.has_value() )
            {
                Stop();
            }
            Save();
        }

        x += Px( 28 ) + gap;
        ImGui::SetCursorScreenPos( ImVec2( x, origin.y ) );
        toggle |= PlayButton( m_timer.has_value() );

        ImGui::SetCursorScreenPos( origin );
        ImGui::Dummy( ImVec2( avail, rowHeight ) );

        if( !m_timer.has_value() && ( descriptionFocused || m_suggestionsHovered ) )
        {
            DrawSuggestions();
        }
        else
        {
            m_suggestionsHovered = false;
        }
        EndCard();

        if( toggle )
        {
            Toggle();
        }
    }

    bool App::DrawWatchButton()
    {
        bool const on = m_settings.m_tracking;
        ImVec2 const pos = ImGui::GetCursorScreenPos();
        float const size = Px( 28 );
        bool const clicked = ImGui::InvisibleButton( "##watch", ImVec2( size, size ) );
        bool const hovered = ImGui::IsItemHovered();
        ImVec2 const c( pos.x + size / 2, pos.y + size / 2 );
        ImU32 const color = on ? g_green : g_muted;

        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        pDrawList->AddCircle( c, Px( 12 ), color, 0, Px( hovered ? 2.0f : 1.5f ) );
        if( on )
        {
            for( float dx : { -3.0f, 3.0f } )
            {
                pDrawList->AddLine( ImVec2( c.x + Px( dx ), c.y - Px( 4.5f ) ), ImVec2( c.x + Px( dx ), c.y + Px( 4.5f ) ), color, Px( 2.5f ) );
            }
        }
        else
        {
            pDrawList->AddTriangleFilled( ImVec2( c.x - Px( 3 ), c.y - Px( 5 ) ), ImVec2( c.x + Px( 5 ), c.y ), ImVec2( c.x - Px( 3 ), c.y + Px( 5 ) ), color );
        }

        if( !on )
        {
            ImGui::SetItemTooltip( "Auto-tracking paused. Click to resume." );
            return clicked;
        }

        std::string watching = "Watching: nothing (tracker stopped)";
        if( Tracker::IsAlive( m_status ) && !m_status->m_app.empty() )
        {
            watching = "Watching: " + m_status->m_app + " " + m_status->m_title;
        }

        std::string tracking = "Tracking: nothing";
        if( m_timer.has_value() )
        {
            tracking = "Tracking: " + m_timer->m_description + ( m_timer->m_rule.has_value() ? " (by rule)" : " (by hand)" );
        }

        ImGui::SetItemTooltip( "%s\n%s\nClick to pause auto-tracking.", watching.c_str(), tracking.c_str() );
        return clicked;
    }

    void App::DrawSuggestions()
    {
        // Earlier activities matching what's typed; one click starts it
        std::string const query = Core::ToLower( Core::Trim( m_description ) );
        std::vector<Activity> matches;
        for( Activity const& activity : m_activities )
        {
            std::string const lower = Core::ToLower( activity.m_description );
            if( query.empty() || ( Core::Contains( lower, query ) && lower != query ) )
            {
                matches.push_back( activity );
            }

            if( matches.size() == 6 )
            {
                break;
            }
        }

        if( matches.empty() )
        {
            m_suggestionsHovered = false;
            return;
        }

        float const rightEdge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        float const spacing = ImGui::GetStyle().ItemSpacing.x;
        std::optional<Activity> start;
        ImGui::BeginGroup();
        ImGui::AlignTextToFramePadding();
        Label( query.empty() ? "Recent" : "Matches", 12.5f, g_muted );
        ImGui::PushFont( nullptr, 13.5f );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, Px( 14 ) );
        for( size_t i = 0; i < matches.size(); i++ )
        {
            Activity const& activity = matches[i];
            std::string const label = activity.m_project.empty() ? activity.m_description : activity.m_description + "  ·  " + activity.m_project;
            SameLineIfFits( ImGui::CalcTextSize( label.c_str() ).x + ImGui::GetStyle().FramePadding.x * 2, rightEdge, spacing );

            ImGui::PushID( static_cast<int>( i ) );
            ImGui::PushStyleColor( ImGuiCol_Border, Fade( ColorFor( activity.m_project ), 0.6f ) );
            if( ImGui::Button( label.c_str() ) )
            {
                start = activity;
            }
            ImGui::PopStyleColor();
            ImGui::SetItemTooltip( "Start this" );
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        ImGui::PopFont();
        ImGui::EndGroup();

        // Clicking a pill takes focus from the text box, so keep showing them while the mouse is over them
        ImVec2 const pad( Px( 6 ), Px( 6 ) );
        ImVec2 const min = ImGui::GetItemRectMin();
        ImVec2 const max = ImGui::GetItemRectMax();
        m_suggestionsHovered = ImGui::IsMouseHoveringRect( ImVec2( min.x - pad.x, min.y - pad.y ), ImVec2( max.x + pad.x, max.y + pad.y ), false );

        if( start.has_value() )
        {
            m_description.clear();
            m_project.clear();
            Start( start->m_description, start->m_project );
        }
    }
}
