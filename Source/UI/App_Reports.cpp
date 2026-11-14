#include "UI/App.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>

#include "UI/Widgets.h"

//-------------------------------------------------------------------------
// Reports page: week totals as tiles, a bar chart per day, and time per project.
// Summary stacks the bars by project; Workload compares each weekday with the work-hours target.
//-------------------------------------------------------------------------

namespace Kui::UI
{
    struct LegendItem
    {
        ImU32           m_color = 0;
        std::string     m_name;
    };

    //-------------------------------------------------------------------------

    void App::DrawReports()
    {
        Core::Day const today = Core::DayOf( Core::Now() );
        ImGui::AlignTextToFramePadding();
        Label( "Reports", 22.0f, g_text, true );
        ImGui::SameLine( 0, Px( 24 ) );
        DrawWeekNav( today );

        // Summary / Workload tabs
        constexpr char const* tabs[] = { "Summary", "Workload" };
        for( int i = 0; i < 2; i++ )
        {
            bool const on = m_reportTab == i;
            if( i > 0 )
            {
                ImGui::SameLine( 0, Px( 18 ) );
            }

            if( FlatButton( tabs[i], tabs[i], 15.0f, on ? g_text : g_muted, on ) )
            {
                m_reportTab = i;
            }

            if( on )
            {
                ImVec2 const min = ImGui::GetItemRectMin();
                ImVec2 const max = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddLine( ImVec2( min.x, max.y + Px( 2 ) ), ImVec2( max.x, max.y + Px( 2 ) ), g_accent, Px( 2 ) );
            }
        }

        bool const isWorkload = m_reportTab == 1;
        if( isWorkload )
        {
            float const dragWidth = Px( 80 );
            char const* pLabel = "Work hours per weekday";
            ImGui::SameLine( 0, 0 );
            ImGui::SetCursorPosX( ImGui::GetWindowWidth() - dragWidth - ImGui::CalcTextSize( pLabel ).x - ImGui::GetStyle().ItemSpacing.x );
            Label( pLabel, g_bodySize, g_muted );
            ImGui::SameLine();
            ImGui::SetNextItemWidth( dragWidth );
            if( ImGui::DragFloat( "##workHours", &m_settings.m_workHours, 0.25f, 1.0f, 24.0f, "%g h", ImGuiSliderFlags_AlwaysClamp ) )
            {
                Save();
            }
        }

        // Week numbers
        float const workHours = m_settings.m_workHours;
        auto const target = [workHours]( int i ) { return i < 5 ? static_cast<int64_t>( workHours * 3600.0f ) : 0; };
        Core::Day const monday = Core::WeekMonday( today, m_week );
        std::vector<ProjectSecs> days;
        std::vector<int64_t> totals;
        std::vector<int64_t> over;
        int64_t total = 0;
        int64_t targetTotal = 0;
        int activeDays = 0;
        int best = 0;
        ProjectSecs byProject;
        for( int i = 0; i < 7; i++ )
        {
            days.push_back( DayParts( monday + i ) );
            int64_t dayTotal = 0;
            for( auto const& [project, secs] : days.back() )
            {
                dayTotal += secs;
                auto const found = std::ranges::find( byProject, project, &ProjectSecs::value_type::first );
                if( found != byProject.end() )
                {
                    found->second += secs;
                }
                else
                {
                    byProject.emplace_back( project, secs );
                }
            }

            totals.push_back( dayTotal );
            over.push_back( std::max<int64_t>( dayTotal - target( i ), 0 ) );
            total += dayTotal;
            targetTotal += target( i );
            activeDays += dayTotal > 0 ? 1 : 0;
            best = dayTotal > totals[best] ? i : best;
        }
        std::ranges::stable_sort( byProject, std::greater<>(), &ProjectSecs::value_type::second );

        int64_t overTotal = 0;
        for( int64_t secs : over )
        {
            overTotal += secs;
        }

        std::vector<LegendItem> legend;
        if( isWorkload )
        {
            legend = { { Fade( g_accent, 0.35f ), "Work hours" }, { g_accent, "Logged time" }, { g_red, "Overtime" } };
        }
        else
        {
            for( auto const& [project, secs] : byProject )
            {
                legend.push_back( { ColorFor( project ), project } );
            }
        }

        ImGui::Dummy( ImVec2( 0, Px( 2 ) ) );
        ImGui::BeginChild( "reports" );
        float const gap = Px( 14 );

        // Tiles
        std::pair<char const*, std::string> tiles[3];
        if( isWorkload )
        {
            tiles[0] = { "Work hours", Core::FormatHM( targetTotal ) };
            tiles[1] = { "Logged time", Core::FormatHM( total ) };
            tiles[2] = { "Overtime", Core::FormatHM( overTotal ) };
        }
        else
        {
            tiles[0] = { "Logged time", Core::FormatHM( total ) };
            tiles[1] = { "Average per active day", Core::FormatHM( total / std::max( activeDays, 1 ) ) };
            tiles[2] = { "Busiest day", totals[best] > 0 ? Core::FormatDay( monday + best, "{:%a}" ) + " · " + Core::FormatHM( totals[best] ) : "–" };
        }

        float const tileWidth = ( ImGui::GetContentRegionAvail().x - gap * 2 ) / 3;
        for( int i = 0; i < 3; i++ )
        {
            if( i > 0 )
            {
                ImGui::SameLine( 0, gap );
            }

            ImGui::PushID( i );
            BeginCard( "tile", g_line, 18.0f, 12.0f, g_card, tileWidth );
            Label( tiles[i].first, 13.5f, g_muted );
            Label( tiles[i].second, 25.0f, g_text, true );
            EndCard();
            ImGui::PopID();
        }
        ImGui::Spacing();

        // Bar chart
        BeginCard( "chart" );
        Label( isWorkload ? "Workload by day" : "Logged time by day", 15.5f, g_text, true );
        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        ImVec2 const chartMin = ImGui::GetCursorScreenPos();
        ImVec2 const chartMax( chartMin.x + ImGui::GetContentRegionAvail().x, chartMin.y + Px( 280 ) );
        ImGui::Dummy( ImVec2( chartMax.x - chartMin.x, chartMax.y - chartMin.y ) );

        ImVec2 const plotMin( chartMin.x + Px( 44 ), chartMin.y + Px( 22 ) );
        ImVec2 const plotMax( chartMax.x, chartMax.y - Px( 40 ) );
        float const plotHeight = plotMax.y - plotMin.y;

        int64_t maxSecs = 0;
        for( int i = 0; i < 7; i++ )
        {
            maxSecs = std::max( { maxSecs, totals[i], target( i ) } );
        }

        float const maxHours = static_cast<float>( maxSecs ) / 3600.0f;
        float step = 100.0f;
        for( float s : { 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f, 20.0f, 50.0f } )
        {
            if( maxHours / s <= 5.0f )
            {
                step = s;
                break;
            }
        }

        float const topHours = std::max( std::ceil( maxHours / step ), 1.0f ) * step;
        int const steps = static_cast<int>( std::round( topHours / step ) );
        for( int k = 0; k <= steps; k++ )
        {
            float const hours = static_cast<float>( k ) * step;
            float const y = plotMax.y - plotHeight * ( hours / topHours );
            pDrawList->AddLine( ImVec2( plotMin.x, y ), ImVec2( plotMax.x, y ), Fade( g_line, k == 0 ? 1.0f : 0.5f ) );
            std::string const label = ( hours == std::floor( hours ) ) ? std::format( "{}h", static_cast<int>( hours ) ) : std::format( "{}m", static_cast<int>( hours * 60 ) );
            TextAt( pDrawList, ImVec2( plotMin.x - Px( 10 ), y ), label, 12.0f, g_muted, false, 1.0f );
        }

        auto const yOf = [&]( int64_t secs ) { return plotMax.y - plotHeight * ( static_cast<float>( secs ) / 3600.0f / topHours ); };
        float const slot = ( plotMax.x - plotMin.x ) / 7;
        for( int i = 0; i < 7; i++ )
        {
            Core::Day const day = monday + i;
            ImVec2 const slotMin( plotMin.x + slot * static_cast<float>( i ), chartMin.y );
            ImVec2 const slotMax( slotMin.x + slot, chartMax.y );
            float const cx = slotMin.x + slot / 2;
            bool const hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect( slotMin, slotMax );
            if( hovered )
            {
                pDrawList->AddRectFilled( slotMin, slotMax, Fade( g_accent, 0.06f ), Px( 8 ) );
            }

            float const barWidth = std::min( slot * 0.5f, Px( 90 ) );
            float const x = cx - barWidth / 2;
            float y = plotMax.y;
            if( isWorkload )
            {
                int64_t const goal = target( i );
                float const wide = std::min( slot * 0.86f, Px( 120 ) );
                if( goal > 0 )
                {
                    pDrawList->AddRectFilled( ImVec2( cx - wide / 2, yOf( goal ) ), ImVec2( cx + wide / 2, plotMax.y ), Fade( g_accent, 0.14f ), Px( 3 ) );
                    pDrawList->AddLine( ImVec2( cx - wide / 2, yOf( goal ) ), ImVec2( cx + wide / 2, yOf( goal ) ), g_accent, Px( 2 ) );
                }

                int64_t const ok = std::min( totals[i], goal );
                pDrawList->AddRectFilled( ImVec2( x, yOf( ok ) ), ImVec2( x + barWidth, plotMax.y ), g_accent, Px( 3 ) );
                if( over[i] > 0 )
                {
                    pDrawList->AddRectFilled( ImVec2( x, yOf( totals[i] ) ), ImVec2( x + barWidth, yOf( ok ) ), g_red, Px( 3 ) );
                }
                y = yOf( totals[i] );
            }
            else
            {
                // Bottom-up stack with 1px gaps between projects reads cleaner than butted blocks
                for( auto const& [project, secs] : days[i] )
                {
                    float const h = plotHeight * ( static_cast<float>( secs ) / 3600.0f / topHours );
                    float const gapPx = y < plotMax.y ? 1.0f : 0.0f;
                    pDrawList->AddRectFilled( ImVec2( x, y - h ), ImVec2( x + barWidth, y - gapPx ), ColorFor( project ), Px( 3 ) );
                    y -= h;
                }
            }

            bool const showOver = isWorkload && over[i] > 0;
            if( totals[i] > 0 )
            {
                std::string const text = showOver ? std::format( "{} (+{})", Core::FormatHM( totals[i] ), Core::FormatHM( over[i] ) ) : Core::FormatHM( totals[i] );
                TextAt( pDrawList, ImVec2( cx, y - Px( 9 ) ), text, 12.0f, showOver ? g_red : g_muted, false, 0.5f );
            }

            bool const isToday = day == today;
            TextAt( pDrawList, ImVec2( cx, plotMax.y + Px( 14 ) ), Core::FormatDay( day, "{:%a}" ), 14.0f, isToday ? g_accent : g_text, isToday, 0.5f );
            TextAt( pDrawList, ImVec2( cx, plotMax.y + Px( 31 ) ), Core::DayMonth( day ), 12.0f, g_muted, false, 0.5f );

            if( hovered && totals[i] > 0 )
            {
                std::string tip = Core::FormatDay( day, "{:%A}" ) + ", " + Core::DayMonth( day, true ) + "\nTotal  " + Core::FormatHMS( totals[i] );
                if( isWorkload )
                {
                    tip += "\nWork hours  " + Core::FormatHMS( target( i ) ) + "\nOvertime  " + Core::FormatHMS( over[i] );
                }
                else
                {
                    for( auto const& [project, secs] : days[i] )
                    {
                        tip += "\n" + ( project.empty() ? std::string( "No project" ) : project ) + "  " + Core::FormatHMS( secs );
                    }
                }
                ImGui::SetTooltip( "%s", tip.c_str() );
            }
        }

        // Legend
        float const rightEdge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        for( size_t i = 0; i < legend.size(); i++ )
        {
            std::string const name = legend[i].m_name.empty() ? "No project" : legend[i].m_name;
            if( i > 0 )
            {
                SameLineIfFits( Px( 18 ) + TextWidth( name, 13.0f ), rightEdge, Px( 16 ) );
            }

            ImGui::BeginGroup();
            Dot( legend[i].m_color );
            ImGui::SameLine( 0, Px( 6 ) );
            Label( name, 13.0f, g_muted );
            ImGui::EndGroup();
        }
        EndCard();

        // Time per project
        if( !isWorkload && total > 0 )
        {
            ImGui::Spacing();
            BeginCard( "byProject" );
            Label( "By project", 15.5f, g_text, true );
            float const max = static_cast<float>( std::max<int64_t>( byProject.front().second, 1 ) );
            pDrawList = ImGui::GetWindowDrawList();
            for( auto const& [project, secs] : byProject )
            {
                ImVec2 const pos = ImGui::GetCursorScreenPos();
                float const width = ImGui::GetContentRegionAvail().x;
                float const centerY = pos.y + Px( 11 );
                float const right = pos.x + width;
                pDrawList->AddCircleFilled( ImVec2( pos.x + Px( 5 ), centerY ), Px( 4.5f ), ColorFor( project ) );
                TextAt( pDrawList, ImVec2( pos.x + Px( 18 ), centerY ), project.empty() ? "No project" : project, 14.0f, g_text, false, 0.0f, Px( 166 ) );

                std::string const percent = std::format( "{:.0f}%", static_cast<float>( secs ) * 100.0f / static_cast<float>( total ) );
                TextAt( pDrawList, ImVec2( right, centerY ), percent, g_bodySize, g_muted, false, 1.0f );
                TextAt( pDrawList, ImVec2( right - Px( 56 ), centerY ), Core::FormatHM( secs ), 14.0f, g_text, true, 1.0f );

                float const barMin = pos.x + Px( 196 );
                float const barMax = right - Px( 134 );
                if( barMax > barMin )
                {
                    float const fill = std::max( ( barMax - barMin ) * static_cast<float>( secs ) / max, Px( 6 ) );
                    pDrawList->AddRectFilled( ImVec2( barMin, centerY - Px( 4 ) ), ImVec2( barMax, centerY + Px( 4 ) ), Fade( g_line, 0.6f ), Px( 4 ) );
                    pDrawList->AddRectFilled( ImVec2( barMin, centerY - Px( 4 ) ), ImVec2( barMin + fill, centerY + Px( 4 ) ), ColorFor( project ), Px( 4 ) );
                }
                ImGui::Dummy( ImVec2( width, Px( 22 ) ) );
            }
            EndCard();
        }
        ImGui::EndChild();
    }
}
