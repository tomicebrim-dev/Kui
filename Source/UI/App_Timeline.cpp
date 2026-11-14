#include "UI/App.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <format>

#include "Core/Layout.h"
#include "UI/Widgets.h"

//-------------------------------------------------------------------------
// Timeline page: one row per project (or activity), one column per day. Days in a row with tracked time
// join into a bar. Drag a bar onto another project's row to move its entries there.
//-------------------------------------------------------------------------

namespace Kui::UI
{
    struct TimelineRow
    {
        std::string                                     m_name;
        std::string                                     m_project;      // Color, and the drop target in project mode
        int64_t                                         m_total = 0;
        std::vector<int64_t>                            m_daySecs;
        std::vector<std::map<std::string, int64_t>>     m_dayParts;     // Per day: activity (or project) -> secs
        std::vector<std::vector<Core::Entry const*>>    m_dayEntries;
    };

    //-------------------------------------------------------------------------

    void App::DrawTimeline()
    {
        Core::Day const today = Core::DayOf( Core::Now() );
        ImGui::AlignTextToFramePadding();
        DrawWeekNav( today );

        // Right-aligned switches: grouping and range
        auto const toggle = [&]( char const* pLabel, bool on )
        {
            ImGui::PushStyleColor( ImGuiCol_Button, on ? g_accentDark : g_card );
            ImGui::PushStyleColor( ImGuiCol_Border, on ? g_accent : g_line );
            bool const clicked = ImGui::Button( pLabel );
            ImGui::PopStyleColor( 2 );
            ImGui::SameLine( 0, Px( 4 ) );
            return clicked;
        };

        constexpr char const* labels[] = { "Projects", "Activities", "2 weeks", "4 weeks" };
        float switchWidth = Px( 16 ) + Px( 4 ) * 3;
        for( char const* pLabel : labels )
        {
            switchWidth += ImGui::CalcTextSize( pLabel ).x + ImGui::GetStyle().FramePadding.x * 2;
        }
        ImGui::SameLine( 0, 0 );
        ImGui::SetCursorPosX( std::max( ImGui::GetCursorPosX() + Px( 16 ), ImGui::GetWindowWidth() - switchWidth ) );
        m_timelineByActivity = toggle( "Projects", !m_timelineByActivity ) ? false : m_timelineByActivity;
        m_timelineByActivity = toggle( "Activities", m_timelineByActivity ) ? true : m_timelineByActivity;
        ImGui::SameLine( 0, Px( 16 ) );
        m_timelineWeeks = toggle( "2 weeks", m_timelineWeeks == 2 ) ? 2 : m_timelineWeeks;
        m_timelineWeeks = toggle( "4 weeks", m_timelineWeeks == 4 ) ? 4 : m_timelineWeeks;
        ImGui::NewLine();

        // The shown weeks end with the picked one
        int const dayCount = m_timelineWeeks * 7;
        int const firstWeek = m_week - m_timelineWeeks + 1;
        Core::Day const first = Core::WeekMonday( today, firstWeek );
        std::optional<Core::Entry> live;
        if( m_timer.has_value() )
        {
            live = Core::Entry{ m_timer->m_start, Core::Now(), m_timer->m_description, m_timer->m_project, m_timer->m_rule.has_value() };
        }

        std::vector<TimelineRow> rows;
        std::map<std::string, size_t> rowOf;
        auto const rowFor = [&]( std::string const& key, std::string const& name, std::string const& project ) -> TimelineRow&
        {
            auto const [found, isNew] = rowOf.emplace( key, rows.size() );
            if( isNew )
            {
                rows.push_back( TimelineRow{ name, project, 0, std::vector<int64_t>( dayCount ), std::vector<std::map<std::string, int64_t>>( dayCount ), std::vector<std::vector<Core::Entry const*>>( dayCount ) } );
            }
            return rows[found->second];
        };

        std::vector<int64_t> dayTotals( dayCount );
        for( int i = 0; i < dayCount; i++ )
        {
            int64_t const from = Core::DayStart( first + i );
            int64_t const to = Core::DayStart( first + i + 1 );
            std::vector<Core::Entry const*> entries = Core::Span( m_entries, from, to, m_longestEntry );
            if( live.has_value() && live->m_end > from && live->m_start < to )
            {
                entries.push_back( &*live );
            }

            for( Core::Entry const* pEntry : entries )
            {
                int64_t const secs = std::min( pEntry->m_end, to ) - std::max( pEntry->m_start, from );
                if( secs <= 0 )
                {
                    continue;
                }

                std::string const name = pEntry->m_description.empty() ? "(no description)" : pEntry->m_description;
                std::string const projectName = pEntry->m_project.empty() ? "No project" : pEntry->m_project;
                TimelineRow& row = m_timelineByActivity
                    ? rowFor( pEntry->m_description + '\x1f' + pEntry->m_project, name, pEntry->m_project )
                    : rowFor( pEntry->m_project, projectName, pEntry->m_project );
                row.m_total += secs;
                row.m_daySecs[i] += secs;
                row.m_dayParts[i][m_timelineByActivity ? projectName : name] += secs;
                row.m_dayEntries[i].push_back( pEntry );
                dayTotals[i] += secs;
            }
        }
        std::ranges::stable_sort( rows, std::greater<>(), &TimelineRow::m_total );

        // Every known project gets a row, so a bar can be dragged onto one with no time in these weeks
        if( !m_timelineByActivity )
        {
            for( std::string const& project : m_projects )
            {
                rowFor( project, project, project );
            }
        }

        if( rows.empty() )
        {
            ImGui::Dummy( ImVec2( 0, Px( 24 ) ) );
            Label( "Nothing tracked in these weeks", g_bodySize, g_muted );
            return;
        }

        // Columns shrink to fit; weekday names drop out when they no longer fit
        float const width = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
        float const labelWidth = std::clamp( width * 0.2f, Px( 120 ), Px( 190 ) );
        float const columnWidth = std::max( ( width - labelWidth ) / static_cast<float>( dayCount ), Px( 18 ) );
        float const rowHeight = Px( 52 );
        float const headerHeight = Px( 62 );
        int64_t const goalSecs = static_cast<int64_t>( m_settings.m_workHours * 3600.0f );

        // Header: goal diamonds, weekday, date
        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        ImVec2 const header = ImGui::GetCursorScreenPos();
        float const gridX = header.x + labelWidth;
        auto const columnX = [&]( int i ) { return gridX + columnWidth * static_cast<float>( i ); };
        for( int i = 0; i < dayCount; i++ )
        {
            Core::Day const day = first + i;
            bool const isToday = day == today;
            bool const isWeekend = Core::WeekdayIso( day ) >= 6;
            float const cx = columnX( i ) + columnWidth / 2;
            if( isWeekend )
            {
                pDrawList->AddRectFilled( ImVec2( columnX( i ), header.y ), ImVec2( columnX( i + 1 ), header.y + headerHeight ), Fade( g_field, 0.6f ) );
            }

            // Diamond on days that reached the daily work-hours goal
            if( goalSecs > 0 && dayTotals[i] >= goalSecs )
            {
                ImVec2 const c( cx, header.y + Px( 9 ) );
                float const r = Px( 5.5f );
                pDrawList->AddQuadFilled( ImVec2( c.x, c.y - r ), ImVec2( c.x + r, c.y ), ImVec2( c.x, c.y + r ), ImVec2( c.x - r, c.y ), g_accent );
                if( ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect( ImVec2( cx - r * 2, c.y - r * 2 ), ImVec2( cx + r * 2, c.y + r * 2 ) ) )
                {
                    ImGui::SetTooltip( "Work-hours goal reached\n%s: %s", Core::DayMonth( day ).c_str(), Core::FormatHM( dayTotals[i] ).c_str() );
                }
            }

            ImU32 const color = isToday ? g_accent : ( isWeekend ? Fade( g_muted, 0.7f ) : g_muted );
            if( columnWidth >= Px( 36 ) )
            {
                std::string name = Core::FormatDay( day, "{:%a}" );
                std::ranges::transform( name, name.begin(), []( unsigned char c ) { return static_cast<char>( std::toupper( c ) ); } );
                TextAt( pDrawList, ImVec2( cx, header.y + Px( 27 ) ), name, 11.5f, color, true, 0.5f );
            }
            TextAt( pDrawList, ImVec2( cx, header.y + Px( 46 ) ), std::to_string( Core::DayOfMonth( day ) ), 15.0f, isToday ? g_accent : g_text, true, 0.5f );
        }
        pDrawList->AddLine( ImVec2( header.x, header.y + headerHeight ), ImVec2( header.x + width, header.y + headerHeight ), g_line );
        ImGui::Dummy( ImVec2( width, headerHeight ) );

        ImGui::BeginChild( "timelineBody" );
        pDrawList = ImGui::GetWindowDrawList();
        ImVec2 const top = ImGui::GetCursorScreenPos();
        float const bottom = top.y + std::max( rowHeight * static_cast<float>( rows.size() ), ImGui::GetContentRegionAvail().y );
        ImVec2 const mouse = ImGui::GetMousePos();
        bool const dragging = m_timelineDrag.has_value();

        // Row under the mouse, the drop target while dragging
        std::optional<size_t> hoveredRow;
        if( ImGui::IsWindowHovered( ImGuiHoveredFlags_AllowWhenBlockedByActiveItem ) && mouse.y >= top.y )
        {
            size_t const row = static_cast<size_t>( ( mouse.y - top.y ) / rowHeight );
            hoveredRow = row < rows.size() ? std::optional<size_t>( row ) : std::nullopt;
        }

        // Grid: weekend and today columns, day lines (stronger between weeks), now line
        for( int i = 0; i < dayCount; i++ )
        {
            Core::Day const day = first + i;
            float const x = columnX( i );
            if( Core::WeekdayIso( day ) >= 6 )
            {
                pDrawList->AddRectFilled( ImVec2( x, top.y ), ImVec2( x + columnWidth, bottom ), Fade( g_field, 0.6f ) );
            }
            if( day == today )
            {
                pDrawList->AddRectFilled( ImVec2( x, top.y ), ImVec2( x + columnWidth, bottom ), Fade( g_accent, 0.04f ) );
            }
            pDrawList->AddLine( ImVec2( x, top.y ), ImVec2( x, bottom ), Fade( g_line, Core::WeekdayIso( day ) == 1 ? 1.0f : 0.45f ) );
        }

        for( size_t r = 0; r < rows.size(); r++ )
        {
            float const y = top.y + rowHeight * static_cast<float>( r );
            bool const isTarget = dragging && hoveredRow == r && rows[r].m_project != m_timelineDrag->m_project;
            if( isTarget )
            {
                pDrawList->AddRectFilled( ImVec2( top.x, y ), ImVec2( top.x + width, y + rowHeight ), Fade( g_accent, 0.08f ) );
            }
            pDrawList->AddLine( ImVec2( top.x, y + rowHeight ), ImVec2( top.x + width, y + rowHeight ), Fade( g_line, 0.55f ) );
        }

        if( today >= first && today < first + dayCount )
        {
            int64_t const dayStart = Core::DayStart( today );
            float const fraction = static_cast<float>( Core::Now() - dayStart ) / static_cast<float>( Core::DayStart( today + 1 ) - dayStart );
            float const x = columnX( today - first ) + columnWidth * fraction;
            pDrawList->AddLine( ImVec2( x, top.y ), ImVec2( x, bottom ), Fade( g_red, 0.6f ), Px( 1.5f ) );
        }

        // Rows: label, then one bar per run of tracked days
        for( size_t r = 0; r < rows.size(); r++ )
        {
            TimelineRow const& row = rows[r];
            float const y = top.y + rowHeight * static_cast<float>( r );
            float const cy = y + rowHeight / 2;
            ImU32 const color = ColorFor( row.m_project );
            pDrawList->AddCircleFilled( ImVec2( top.x + Px( 6 ), cy - Px( 7 ) ), Px( 4.5f ), color );
            TextAt( pDrawList, ImVec2( top.x + Px( 18 ), cy - Px( 7 ) ), row.m_name, 13.5f, g_text, true, 0.0f, labelWidth - Px( 26 ) );
            std::string sub = row.m_total > 0 ? Core::FormatHM( row.m_total ) : "–";
            if( m_timelineByActivity )
            {
                sub = ( row.m_project.empty() ? "No project" : row.m_project ) + " · " + sub;
            }
            TextAt( pDrawList, ImVec2( top.x + Px( 18 ), cy + Px( 10 ) ), sub, 12.0f, g_muted, false, 0.0f, labelWidth - Px( 26 ) );

            for( auto const [a, b] : Core::Runs( row.m_daySecs ) )
            {
                int64_t total = 0;
                std::map<std::string, int64_t> parts;
                std::vector<Core::Entry> entries;   // Files only, not the running timer
                bool hasLive = false;
                for( size_t d = a; d <= b; d++ )
                {
                    total += row.m_daySecs[d];
                    for( auto const& [part, secs] : row.m_dayParts[d] )
                    {
                        parts[part] += secs;
                    }

                    for( Core::Entry const* pEntry : row.m_dayEntries[d] )
                    {
                        bool const isLive = live.has_value() && pEntry == &*live;
                        hasLive |= isLive;
                        bool const isSeen = std::ranges::any_of( entries, [pEntry]( Core::Entry const& e ) { return e.m_start == pEntry->m_start && e.m_end == pEntry->m_end && e.m_description == pEntry->m_description; } );
                        if( !isLive && !isSeen )
                        {
                            entries.push_back( *pEntry );
                        }
                    }
                }

                std::vector<std::pair<std::string, int64_t>> ranked( parts.begin(), parts.end() );
                std::ranges::stable_sort( ranked, std::greater<>(), &std::pair<std::string, int64_t>::second );
                std::string const label = m_timelineByActivity ? row.m_name : ranked.front().first;

                ImVec2 const min( columnX( static_cast<int>( a ) ) + Px( 3 ), y + Px( 9 ) );
                ImVec2 const max( columnX( static_cast<int>( b ) + 1 ) - Px( 3 ), y + rowHeight - Px( 9 ) );
                ImGui::SetCursorScreenPos( min );
                ImGui::PushID( static_cast<int>( r * 1000 + a ) );
                ImGui::InvisibleButton( "bar", ImVec2( std::max( max.x - min.x, 1.0f ), max.y - min.y ) );
                ImGui::PopID();
                bool const hovered = ImGui::IsItemHovered() && !dragging;
                bool const canDrag = !m_timelineByActivity && !entries.empty();
                if( canDrag && ImGui::IsItemActive() && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) && !dragging )
                {
                    m_timelineDrag = TimelineDrag{ entries, row.m_project, label, first + static_cast<int>( a ), mouse.x - min.x, max.x - min.x };
                }

                // Open the bar's first week in the calendar
                if( hovered && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
                {
                    m_week = firstWeek + static_cast<int>( a ) / 7;
                    m_calendarScrolled = false;
                    m_view = View::Calendar;
                }

                // While its entries are being dragged the bar stays as a faded outline
                bool const isSource = dragging && m_timelineDrag->m_project == row.m_project && m_timelineDrag->m_from == first + static_cast<int>( a );
                float const rounding = Px( 6 );
                pDrawList->AddRectFilled( min, max, Fade( color, isSource ? 0.08f : ( hovered ? 0.42f : 0.28f ) ), rounding );
                pDrawList->AddRectFilled( min, ImVec2( max.x, min.y + Px( 2.5f ) ), Fade( color, isSource ? 0.3f : 1.0f ), rounding, ImDrawFlags_RoundCornersTop );
                if( hasLive )
                {
                    pDrawList->AddRect( min, max, Fade( color, 0.7f ), rounding, 0, Px( 1.5f ) );
                }

                // Name left, hours right; the name goes first when space runs out
                float const textY = ( min.y + max.y ) / 2 + Px( 1 );
                std::string const hours = Core::FormatHM( total );
                float const hoursWidth = TextWidth( hours, 12.0f );
                float const inner = max.x - min.x - Px( 16 );
                if( inner >= hoursWidth )
                {
                    TextAt( pDrawList, ImVec2( max.x - Px( 8 ), textY ), hours, 12.0f, Fade( g_text, 0.7f ), false, 1.0f );
                }
                if( inner - hoursWidth >= Px( 30 ) )
                {
                    TextAt( pDrawList, ImVec2( min.x + Px( 8 ), textY ), label, 13.0f, g_text, true, 0.0f, inner - hoursWidth - Px( 8 ) );
                }

                if( hovered )
                {
                    Core::Day const from = first + static_cast<int>( a );
                    Core::Day const to = first + static_cast<int>( b );
                    std::string tip = row.m_name + "\n" + ( a == b ? Core::DayName( from, today ) : Core::DayMonth( from ) + " – " + Core::DayMonth( to ) );
                    tip += std::format( "  ·  {}", Core::FormatHM( total ) );
                    if( b > a )
                    {
                        tip += std::format( "  ·  {} / day", Core::FormatHM( total / static_cast<int64_t>( b - a + 1 ) ) );
                    }

                    for( size_t k = 0; k < std::min<size_t>( ranked.size(), 5 ); k++ )
                    {
                        tip += "\n  " + ranked[k].first + "  " + Core::FormatHM( ranked[k].second );
                    }
                    if( ranked.size() > 5 )
                    {
                        tip += std::format( "\n  +{} more", ranked.size() - 5 );
                    }

                    tip += canDrag ? "\n\nDrag to another project to move it there\nDouble-click to open in Calendar" : "\n\nDouble-click to open in Calendar";
                    ImGui::SetTooltip( "%s", tip.c_str() );
                }
            }
        }

        // Ghost bar under the mouse; dropping on another project's row moves the entries there
        if( dragging )
        {
            ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeAll );
            float const rowY = hoveredRow.has_value() ? top.y + rowHeight * static_cast<float>( *hoveredRow ) : mouse.y - rowHeight / 2;
            ImVec2 const min( mouse.x - m_timelineDrag->m_grabX, rowY + Px( 9 ) );
            ImVec2 const max( min.x + m_timelineDrag->m_width, rowY + rowHeight - Px( 9 ) );
            ImU32 const color = ColorFor( m_timelineDrag->m_project );
            ImDrawList* pFront = ImGui::GetForegroundDrawList();
            pFront->AddRectFilled( min, max, Fade( color, 0.55f ), Px( 6 ) );
            pFront->AddRect( min, max, color, Px( 6 ), 0, Px( 1.5f ) );
            TextAt( pFront, ImVec2( min.x + Px( 8 ), ( min.y + max.y ) / 2 + Px( 1 ) ), m_timelineDrag->m_label, 13.0f, g_text, true, 0.0f, max.x - min.x - Px( 16 ) );

            if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
            {
                TimelineDrag drag = std::move( *m_timelineDrag );
                m_timelineDrag.reset();
                if( hoveredRow.has_value() && rows[*hoveredRow].m_project != drag.m_project )
                {
                    MoveToProject( std::move( drag.m_entries ), rows[*hoveredRow].m_project );
                }
            }
        }

        ImGui::SetCursorScreenPos( top );
        ImGui::Dummy( ImVec2( width, bottom - top.y ) );
        ImGui::EndChild();
    }
}
