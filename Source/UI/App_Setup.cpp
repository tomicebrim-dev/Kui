#include "UI/App.h"

#include <imgui.h>

#include <algorithm>
#include <cfloat>
#include <format>

#include "UI/Widgets.h"

//-------------------------------------------------------------------------
// First-run setup: step 1 picks installed programs, step 2 time-wasters (sites and Steam games).
//-------------------------------------------------------------------------

namespace Kui::UI
{
    // All/None buttons and a wrapped, scrolling list of checkboxes
    static void PickList( char const* pId, std::vector<Setup::Pick>& items, char const* pEmptyText )
    {
        if( items.empty() )
        {
            Label( pEmptyText, g_bodySize, g_muted );
            return;
        }

        ImGui::PushID( pId );
        for( bool on : { true, false } )
        {
            if( !on )
            {
                ImGui::SameLine();
            }

            if( ImGui::SmallButton( on ? "All" : "None" ) )
            {
                for( Setup::Pick& pick : items )
                {
                    pick.m_on = on;
                }
            }
        }

        ImGui::SetNextWindowSizeConstraints( ImVec2( 0, 0 ), ImVec2( FLT_MAX, Px( 220 ) ) );
        ImGui::BeginChild( "list", ImVec2( 0, 0 ), ImGuiChildFlags_AutoResizeY );
        float const rightEdge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        float const boxWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
        for( size_t i = 0; i < items.size(); i++ )
        {
            if( i > 0 )
            {
                SameLineIfFits( boxWidth + TextWidth( items[i].m_label ), rightEdge, Px( 18 ) );
            }

            ImGui::PushID( static_cast<int>( i ) );
            ImGui::Checkbox( items[i].m_label.c_str(), &items[i].m_on );
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopID();
    }

    //-------------------------------------------------------------------------

    void App::DrawSetup()
    {
        SetupState& setup = *m_setup;
        bool finish = false;
        bool skip = false;

        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( Px( 24 ), Px( 24 ) ) );
        ImGui::BeginChild( "setup", ImVec2( 0, 0 ), ImGuiChildFlags_AlwaysUseWindowPadding );
        ImGui::PopStyleVar();

        // A centered column, at most 620 wide
        float const avail = ImGui::GetContentRegionAvail().x;
        float const width = std::min( avail, Px( 620 ) );
        ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( avail - width ) / 2 );
        ImGui::BeginChild( "column", ImVec2( width, 0 ), ImGuiChildFlags_AutoResizeY );
        ImGui::Dummy( ImVec2( 0, Px( 16 ) ) );
        Caption( std::format( "SETUP · STEP {} OF 2", setup.m_step + 1 ) );

        ImGui::PushTextWrapPos( 0.0f );
        if( setup.m_step == 0 )
        {
            Label( "Which programs do you want to track?", 22.0f, g_text, true );
            Label( "We found these on your computer. Time is tracked automatically while one is in front.", g_bodySize, g_muted );
            ImGui::Dummy( ImVec2( 0, Px( 4 ) ) );
            BeginCard( "apps" );
            PickList( "apps", setup.m_detected.m_apps, "No common creative apps found. You can add any program later on the Programs page." );
            EndCard();
        }
        else
        {
            Label( "Track wasted time too?", 22.0f, g_text, true );
            Label( std::format( "Picked items are logged under the \"{}\" project, so you can see where the hours go.", Setup::g_wasteProject ), g_bodySize, g_muted );
            ImGui::Dummy( ImVec2( 0, Px( 4 ) ) );
            BeginCard( "sites" );
            Caption( "BROWSING (matched by browser tab title)" );
            PickList( "sites", setup.m_detected.m_sites, "" );
            EndCard();

            ImGui::Dummy( ImVec2( 0, Px( 2 ) ) );
            BeginCard( "games" );
            Caption( std::format( "STEAM GAMES ({})", setup.m_detected.m_games.size() ) );
            PickList( "games", setup.m_detected.m_games, "No Steam games found." );
            EndCard();
        }
        ImGui::PopTextWrapPos();

        // Skip on the left; Next / Finish and Back on the right
        ImGui::Dummy( ImVec2( 0, Px( 8 ) ) );
        ImGui::AlignTextToFramePadding();
        skip = FlatButton( "skip", "Skip setup", g_bodySize );

        bool const isLast = setup.m_step == 1;
        float const mainWidth = Px( 96 );
        float const backWidth = ImGui::CalcTextSize( "Back" ).x + ImGui::GetStyle().FramePadding.x * 2;
        float right = ImGui::GetWindowWidth() - mainWidth;
        if( isLast )
        {
            ImGui::SameLine( right - backWidth - ImGui::GetStyle().ItemSpacing.x );
            if( ImGui::Button( "Back" ) )
            {
                setup.m_step = 0;
            }
        }

        ImGui::SameLine( right );
        if( AccentButton( isLast ? "Finish" : "Next", ImVec2( 96, 30 ) ) )
        {
            finish = isLast;
            setup.m_step = 1;
        }
        ImGui::EndChild();
        ImGui::EndChild();

        if( !finish && !skip )
        {
            return;
        }

        if( finish )
        {
            for( Setup::Pick const& pick : setup.m_detected.m_apps )
            {
                if( pick.m_on )
                {
                    AddRule( pick.m_keyword, pick.m_label, "" );
                }
            }

            for( std::vector<Setup::Pick> const* pList : { &setup.m_detected.m_sites, &setup.m_detected.m_games } )
            {
                for( Setup::Pick const& pick : *pList )
                {
                    if( pick.m_on )
                    {
                        AddRule( pick.m_keyword, pick.m_label, Setup::g_wasteProject );
                    }
                }
            }
            m_view = View::Programs;
        }

        m_setup.reset();
        m_settings.m_firstRunDone = true;
        Save();
        Reload( true );
    }
}
