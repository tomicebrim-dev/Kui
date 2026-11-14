#include "UI/App.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <format>

#include "Core/Layout.h"
#include "UI/Widgets.h"

//-------------------------------------------------------------------------
// Calendar page: a week of day columns with tracked time as blocks, plus the week switcher shared with Reports.
//-------------------------------------------------------------------------

namespace Kui::UI
{
    // The part of an entry that falls on one shown day, in seconds since that day's midnight
    struct Piece
    {
        int64_t                 m_start = 0;
        int64_t                 m_end = 0;
        Core::Entry const*      m_pEntry = nullptr;
    };

    // Back-to-back pieces of one activity merged into one
    struct Block
    {
        int64_t                 m_start = 0;
        int64_t                 m_end = 0;
        Core::Entry const*      m_pEntry = nullptr;
        int64_t                 m_tracked = 0;
        size_t                  m_pieces = 1;
    };

    static std::string NameOf( Core::Entry const& entry )
    {
        return entry.m_description.empty() ? "(no description)" : entry.m_description;
    }

    static bool SameActivity( Core::Entry const& a, Core::Entry const& b )
    {
        return a.m_description == b.m_description && a.m_project == b.m_project;
    }

    //-------------------------------------------------------------------------

    void App::DrawWeekNav( Core::Day today )
    {
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( Px( 4 ), ImGui::GetStyle().ItemSpacing.y ) );
        if( ImGui::Button( "‹" ) )
        {
            m_week--;
        }
        ImGui::SetItemTooltip( "Previous week (←)" );

        ImGui::SameLine();
        if( ImGui::Button( "›" ) )
        {
            m_week++;
        }
        ImGui::SetItemTooltip( "Next week (→)" );
        ImGui::PopStyleVar();

        Core::Day const monday = Core::WeekMonday( today, m_week );
        std::string label = Core::DayMonth( monday ) + " – " + Core::DayMonth( monday + 6 );
        if( m_week == 0 )
        {
            label = "This week";
        }
        else if( m_week == -1 )
        {
            label = "Last week";
        }

        ImGui::SameLine( 0, Px( 12 ) );
        Label( label, 16.0f, g_text, true );
        ImGui::SameLine();
        Label( std::format( "W{}", Core::IsoWeek( monday ) ), g_bodySize, g_muted );

        if( m_week != 0 )
        {
            ImGui::SameLine( 0, Px( 16 ) );
            if( ImGui::Button( "Today" ) )
            {
                m_week = 0;
            }
            ImGui::SetItemTooltip( "Back to this week (T)" );
        }
    }

    void App::DrawCalendar()
    {
        Core::Day const today = Core::DayOf( Core::Now() );
        ImGui::AlignTextToFramePadding();
        DrawWeekNav( today );

        // 5 / 7 day switch, right-aligned
        float const switchWidth = ImGui::CalcTextSize( "5 days" ).x + ImGui::CalcTextSize( "7 days" ).x + ImGui::GetStyle().FramePadding.x * 4 + Px( 4 );
        ImGui::SameLine( 0, 0 );
        ImGui::SetCursorPosX( ImGui::GetWindowWidth() - switchWidth );
        for( int days : { 5, 7 } )
        {
            bool const on = m_calendarDays == days;
            ImGui::PushStyleColor( ImGuiCol_Button, on ? g_accentDark : g_card );
            ImGui::PushStyleColor( ImGuiCol_Border, on ? g_accent : g_line );
            if( ImGui::Button( std::format( "{} days", days ).c_str() ) )
            {
                m_calendarDays = days;
            }
            ImGui::PopStyleColor( 2 );
            ImGui::SameLine( 0, Px( 4 ) );
        }
        ImGui::NewLine();

        Core::Day const monday = Core::WeekMonday( today, m_week );
        int const dayCount = m_calendarDays;
        std::optional<Core::Entry> live;
        if( m_timer.has_value() )
        {
            live = Core::Entry{ m_timer->m_start, Core::Now(), m_timer->m_description, m_timer->m_project, m_timer->m_rule.has_value() };
        }

        std::vector<std::vector<Piece>> columns( dayCount );
        for( int i = 0; i < dayCount; i++ )
        {
            int64_t const from = Core::DayStart( monday + i );
            int64_t const to = from + 86400;
            std::vector<Core::Entry const*> entries = Core::Span( m_entries, from, to );
            if( live.has_value() && live->m_end > from && live->m_start < to )
            {
                entries.push_back( &*live );
            }

            for( Core::Entry const* pEntry : entries )
            {
                columns[i].push_back( Piece{ std::max( pEntry->m_start, from ) - from, std::min( pEntry->m_end, to ) - from, pEntry } );
            }
        }

        // Scrollbar width is left out of both header and body so the columns line up
        float const width = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
        float const gutter = Px( g_gutter );
        float const hourHeight = Px( g_hourHeight );
        float const columnWidth = ( width - gutter ) / static_cast<float>( dayCount );

        // Day header: name, date (circled today) and total
        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        ImVec2 const header = ImGui::GetCursorScreenPos();
        for( int i = 0; i < dayCount; i++ )
        {
            Core::Day const day = monday + i;
            bool const isToday = day == today;
            float const cx = header.x + gutter + columnWidth * ( static_cast<float>( i ) + 0.5f );
            std::string name = Core::FormatDay( day, "{:%a}" );
            std::ranges::transform( name, name.begin(), []( unsigned char c ) { return static_cast<char>( std::toupper( c ) ); } );
            DrawText( pDrawList, ImVec2( cx, header.y + Px( 10 ) ), name, 11.5f, isToday ? g_accent : g_muted, true, 0.5f );

            ImVec2 const circle( cx, header.y + Px( 32 ) );
            if( isToday )
            {
                pDrawList->AddCircleFilled( circle, Px( 15 ), g_accent );
            }
            DrawText( pDrawList, circle, std::to_string( Core::DayOfMonth( day ) ), 17.0f, isToday ? g_side : g_text, true, 0.5f );

            int64_t total = 0;
            for( Piece const& piece : columns[i] )
            {
                total += piece.m_end - piece.m_start;
            }
            DrawText( pDrawList, ImVec2( cx, header.y + Px( 56 ) ), total > 0 ? Core::FormatHM( total ) : "–", 12.0f, g_muted, false, 0.5f );
        }
        pDrawList->AddLine( ImVec2( header.x, header.y + Px( 68 ) ), ImVec2( header.x + width, header.y + Px( 68 ) ), g_line );
        ImGui::Dummy( ImVec2( width, Px( 68 ) ) );

        // Open scrolled to an hour before the first entry of the week
        if( !m_calendarScrolled )
        {
            int64_t first = 8 * 3600;
            for( std::vector<Piece> const& column : columns )
            {
                for( Piece const& piece : column )
                {
                    first = std::min( first, piece.m_start );
                }
            }
            ImGui::SetNextWindowScroll( ImVec2( 0, static_cast<float>( std::max<int64_t>( first / 3600 - 1, 0 ) ) * hourHeight ) );
            m_calendarScrolled = true;
        }

        ImGui::BeginChild( "calendarBody" );
        pDrawList = ImGui::GetWindowDrawList();
        ImVec2 const top = ImGui::GetCursorScreenPos();
        float const x0 = top.x + gutter;
        float const bottom = top.y + hourHeight * 24;
        auto const timeY = [&]( int64_t secs ) { return top.y + static_cast<float>( secs ) / 3600.0f * hourHeight; };

        for( int h = 0; h < 24; h++ )
        {
            float const y = top.y + static_cast<float>( h ) * hourHeight;
            pDrawList->AddLine( ImVec2( x0, y ), ImVec2( top.x + width, y ), Fade( g_line, 0.55f ) );
            pDrawList->AddLine( ImVec2( x0, y + hourHeight / 2 ), ImVec2( top.x + width, y + hourHeight / 2 ), Fade( g_line, 0.22f ) );
            if( h > 0 )
            {
                DrawText( pDrawList, ImVec2( x0 - Px( 10 ), y ), std::format( "{:02}:00", h ), 12.0f, g_muted, false, 1.0f );
            }
        }

        for( int i = 0; i < dayCount; i++ )
        {
            Core::Day const day = monday + i;
            int64_t const dayStart = Core::DayStart( day );
            float const x = x0 + columnWidth * static_cast<float>( i );
            pDrawList->AddLine( ImVec2( x, top.y ), ImVec2( x, bottom ), Fade( g_line, 0.55f ) );
            if( day == today )
            {
                pDrawList->AddRectFilled( ImVec2( x, top.y ), ImVec2( x + columnWidth, bottom ), Fade( g_accent, 0.04f ) );
            }

            // Merge back-to-back pieces of one activity (<10 min gap, nothing else between). Merging across other
            // activities made blocks overlap, which split columns into unreadable slivers.
            std::vector<Piece> pieces = columns[i];
            std::ranges::stable_sort( pieces, {}, &Piece::m_start );
            std::vector<Block> blocks;
            for( Piece const& piece : pieces )
            {
                if( !blocks.empty() && SameActivity( *blocks.back().m_pEntry, *piece.m_pEntry ) && piece.m_start - blocks.back().m_end < 600 )
                {
                    Block& block = blocks.back();
                    block.m_end = std::max( block.m_end, piece.m_end );
                    block.m_tracked += piece.m_end - piece.m_start;
                    block.m_pieces++;
                    continue;
                }
                blocks.push_back( Block{ piece.m_start, piece.m_end, piece.m_pEntry, piece.m_end - piece.m_start, 1 } );
            }

            // Runs of short blocks (<15 min, too small to label) become one mixed block named after its activities
            auto const isShort = []( Block const& b ) { return b.m_end - b.m_start < 900; };
            std::vector<std::vector<Block>> groups;
            for( Block const& block : blocks )
            {
                if( !groups.empty() && isShort( block ) && isShort( groups.back().front() ) )
                {
                    int64_t const groupEnd = std::ranges::max( groups.back(), {}, &Block::m_end ).m_end;
                    if( block.m_start - groupEnd < 600 )
                    {
                        groups.back().push_back( block );
                        continue;
                    }
                }
                groups.push_back( { block } );
            }

            // Lanes only for real overlaps (true lengths), so blocks keep the full column width
            std::vector<std::pair<int64_t, int64_t>> spans;
            for( std::vector<Block> const& group : groups )
            {
                int64_t const start = group.front().m_start;
                int64_t const end = std::ranges::max( group, {}, &Block::m_end ).m_end;
                spans.emplace_back( start, std::max( end, start + 1 ) );
            }
            std::vector<std::pair<size_t, size_t>> const lanes = Core::Lanes( spans );

            for( size_t j = 0; j < groups.size(); j++ )
            {
                std::vector<Block> const& group = groups[j];
                auto const [start, end] = spans[j];

                // Activities in this block, longest first
                std::vector<std::pair<Core::Entry const*, int64_t>> activities;
                size_t pieceCount = 0;
                for( Block const& block : group )
                {
                    pieceCount += block.m_pieces;
                    auto const found = std::ranges::find_if( activities, [&block]( auto const& a ) { return SameActivity( *a.first, *block.m_pEntry ); } );
                    if( found != activities.end() )
                    {
                        found->second += block.m_tracked;
                    }
                    else
                    {
                        activities.emplace_back( block.m_pEntry, block.m_tracked );
                    }
                }
                std::ranges::stable_sort( activities, std::greater<>(), &std::pair<Core::Entry const*, int64_t>::second );

                int64_t secs = 0;
                std::string title;
                for( auto const& [pEntry, tracked] : activities )
                {
                    secs += tracked;
                    title += ( title.empty() ? "" : ", " ) + NameOf( *pEntry );
                }

                auto const [lane, laneCount] = lanes[j];
                float const laneWidth = ( columnWidth - Px( 6 ) ) / static_cast<float>( laneCount );
                float const height = std::max( timeY( end ) - timeY( start ) - 1.0f, Px( 3 ) );
                ImVec2 const min( x + Px( 3 ) + laneWidth * static_cast<float>( lane ), timeY( start ) );
                ImVec2 const max( min.x + std::max( laneWidth - 1.0f, Px( 4 ) ), min.y + height );
                bool const hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect( min, max );

                // Thin blocks need a stronger fill to be visible at all
                float const fill = ( height < Px( 10 ) ? 0.75f : 0.28f ) + ( hovered ? 0.2f : 0.0f );
                pDrawList->AddRectFilled( min, max, Fade( ColorFor( activities.front().first->m_project ), fill ), std::min( height / 2, Px( 6 ) ) );
                if( height >= Px( 10 ) )
                {
                    // Left bar shows each piece in its own project color, so a mixed block still reads as a timeline
                    for( Block const& block : group )
                    {
                        float const y0 = timeY( block.m_start );
                        float const y1 = std::max( std::min( timeY( block.m_end ), max.y ), y0 + 1.0f );
                        pDrawList->AddRectFilled( ImVec2( min.x, y0 ), ImVec2( min.x + Px( 3 ), y1 ), ColorFor( block.m_pEntry->m_project ), 1.0f );
                    }
                }

                std::string const range = Core::ClockHM( dayStart + start ) + " – " + Core::ClockHM( dayStart + end );
                float const textWidth = max.x - min.x - Px( 13 );
                if( height >= Px( 18 ) )
                {
                    float const titleTop = ( std::min( height, Px( 22 ) ) - Px( 17 ) ) / 2 + Px( 1 );
                    DrawText( pDrawList, ImVec2( min.x + Px( 9 ), min.y + titleTop + Px( 8.5f ) ), title, 12.5f, g_text, true, 0.0f, textWidth );
                }

                if( height >= Px( 38 ) )
                {
                    DrawText( pDrawList, ImVec2( min.x + Px( 9 ), min.y + Px( 28 ) ), range + " · " + Core::FormatHM( secs ), 11.5f, Fade( g_text, 0.7f ), false, 0.0f, textWidth );
                }

                if( hovered )
                {
                    std::string summary = range + "  ·  " + Core::FormatHM( secs ) + " tracked";
                    if( pieceCount > 1 )
                    {
                        summary += std::format( " in {} parts", pieceCount );
                    }

                    std::string tip;
                    if( activities.size() == 1 )
                    {
                        std::string const& project = activities.front().first->m_project;
                        tip = title + "\n" + ( project.empty() ? "" : project + "\n" ) + summary;
                    }
                    else
                    {
                        tip = summary;
                        for( auto const& [pEntry, tracked] : activities )
                        {
                            tip += "\n" + NameOf( *pEntry ) + ( pEntry->m_project.empty() ? "" : " · " + pEntry->m_project ) + "  " + Core::FormatHM( tracked );
                        }
                    }
                    ImGui::SetTooltip( "%s", tip.c_str() );
                }
            }

            if( day == today )
            {
                float const y = timeY( Core::Now() - dayStart );
                pDrawList->AddLine( ImVec2( x, y ), ImVec2( x + columnWidth, y ), g_red, Px( 2 ) );
                pDrawList->AddCircleFilled( ImVec2( x, y ), Px( 4 ), g_red );
            }
        }

        ImGui::Dummy( ImVec2( width, hourHeight * 24 ) );
        ImGui::EndChild();
    }
}
