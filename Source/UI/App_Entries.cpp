#include "UI/App.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <format>
#include <functional>

#include "Core/Text.h"
#include "UI/Widgets.h"

//-------------------------------------------------------------------------
// Timer page: entries grouped per day and activity, expandable into their pieces.
// Rows have fixed heights so only the visible ones are drawn, which keeps years of entries cheap.
//-------------------------------------------------------------------------

namespace Kui::UI
{
    enum class RowKind
    {
        Head,
        Group,
        Item,
    };

    struct Row
    {
        RowKind     m_kind = RowKind::Head;
        size_t      m_index = 0;    // Group index for Head and Group rows, entry index for Item rows
    };

    constexpr float g_headHeight = 38.0f;
    constexpr float g_groupHeight = 42.0f;
    constexpr float g_itemHeight = 28.0f;
    constexpr float g_rowGap = 8.0f;
    constexpr float g_actionSlot = 64.0f;

    //-------------------------------------------------------------------------

    static float RowHeight( RowKind kind )
    {
        switch( kind )
        {
            case RowKind::Head:
            {
                return Px( g_headHeight );
            }
            break;

            case RowKind::Group:
            {
                return Px( g_groupHeight + g_rowGap );
            }
            break;

            case RowKind::Item:
            {
                return Px( g_itemHeight + g_rowGap );
            }
            break;
        }
        return 0.0f;
    }

    // Is the mouse over this rect of the current window (and not over a popup on top)?
    static bool IsHovered( ImVec2 min, ImVec2 max )
    {
        return ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect( min, max );
    }

    //-------------------------------------------------------------------------

    void App::DrawEntries()
    {
        Core::Day const today = Core::DayOf( Core::Now() );
        auto const todayFound = m_daySecs.find( today );
        int64_t const todaySecs = ( todayFound != m_daySecs.end() ? todayFound->second : 0 ) + LiveSecs();
        ImGui::AlignTextToFramePadding();
        Label( "Time entries", 20.0f, g_text, true );
        ImGui::SameLine();
        Label( Core::FormatHM( todaySecs ) + " today", g_bodySize, g_muted );

        if( m_edit.has_value() && ImGui::IsKeyPressed( ImGuiKey_Escape ) )
        {
            m_edit.reset();
        }

        std::vector<Row> rows;
        rows.reserve( m_groups.size() + 64 );
        for( size_t i = 0; i < m_groups.size(); i++ )
        {
            Group const& group = m_groups[i];
            if( i == 0 || m_groups[i - 1].m_key.m_day != group.m_key.m_day )
            {
                rows.push_back( Row{ RowKind::Head, i } );
            }

            rows.push_back( Row{ RowKind::Group, i } );
            if( group.m_items.size() > 1 && m_expanded.contains( group.m_key ) )
            {
                for( size_t item : group.m_items )
                {
                    rows.push_back( Row{ RowKind::Item, item } );
                }
            }
        }

        ImGui::BeginChild( "entries" );
        if( m_groups.empty() )
        {
            float const width = ImGui::GetContentRegionAvail().x;
            auto const centered = [width]( std::string_view text, float size, ImU32 color, bool semibold )
            {
                ImGui::SetCursorPosX( ( width - TextWidth( text, size, semibold ) ) / 2 );
                Label( text, size, color, semibold );
            };

            ImGui::Dummy( ImVec2( 0, Px( 22 ) ) );
            centered( "No time entries yet", 17.0f, g_text, true );
            centered( "Press play above, or let a program track itself.", g_bodySize, g_muted, false );
            char const* pButton = "Add a program to track";
            ImGui::SetCursorPosX( ( width - ImGui::CalcTextSize( pButton ).x ) / 2 - ImGui::GetStyle().FramePadding.x );
            if( ImGui::Button( pButton ) )
            {
                m_view = View::Programs;
            }
            ImGui::EndChild();
            return;
        }

        // Actions run after the loop, since they rebuild m_groups
        std::function<void()> action;
        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        ImVec2 const origin = ImGui::GetCursorScreenPos();
        float const width = ImGui::GetContentRegionAvail().x;
        float const scrollY = ImGui::GetScrollY();
        float const viewHeight = ImGui::GetWindowHeight();
        float y = 0.0f;
        for( Row const& row : rows )
        {
            float const height = RowHeight( row.m_kind );
            bool const visible = y + height > scrollY && y < scrollY + viewHeight;
            ImVec2 const pos( origin.x, origin.y + y );
            y += height;
            if( !visible )
            {
                continue;
            }

            ImGui::PushID( static_cast<int>( row.m_kind ) * 1000003 + static_cast<int>( row.m_index ) );
            switch( row.m_kind )
            {
                case RowKind::Head:
                {
                    Group const& group = m_groups[row.m_index];
                    float const centerY = pos.y + Px( 24 );
                    DrawText( pDrawList, ImVec2( pos.x, centerY ), Core::DayName( group.m_key.m_day, today ), 14.0f, g_muted, true );
                    DrawText( pDrawList, ImVec2( pos.x + width, centerY ), Core::FormatHM( m_daySecs[group.m_key.m_day] ), 13.5f, g_muted, true, 1.0f );
                }
                break;

                case RowKind::Group:
                {
                    Group const& group = m_groups[row.m_index];
                    ImVec2 const max( pos.x + width, pos.y + Px( g_groupHeight ) );
                    float const centerY = pos.y + Px( g_groupHeight ) / 2;
                    bool const hot = IsHovered( pos, max );
                    bool const open = m_expanded.contains( group.m_key );
                    pDrawList->AddRectFilled( pos, max, hot ? g_hover : g_card, Px( 8 ) );

                    std::vector<Core::Entry> pieces;
                    for( size_t item : group.m_items )
                    {
                        pieces.push_back( m_entries[item] );
                    }

                    if( m_edit.has_value() && m_edit->m_key == group.m_key )
                    {
                        // Rename in place: name, project, Save, Cancel
                        float const projectWidth = 170.0f;
                        float const left = pos.x + Px( 14 );
                        float const descriptionWidth = std::max( max.x - left - Px( 14 ) - Px( projectWidth ) - Px( 160 ), Px( 80 ) );
                        ImGui::SetCursorScreenPos( ImVec2( left, centerY - ImGui::GetFrameHeight() / 2 ) );
                        if( std::exchange( m_edit->m_needsFocus, false ) )
                        {
                            ImGui::SetKeyboardFocusHere();
                        }

                        ImGui::SetNextItemWidth( descriptionWidth );
                        ImGui::InputTextWithHint( "##name", "Description", &m_edit->m_description );
                        bool const enter = ImGui::IsItemDeactivated() && ( ImGui::IsKeyPressed( ImGuiKey_Enter ) || ImGui::IsKeyPressed( ImGuiKey_KeypadEnter ) );

                        ImGui::SetCursorScreenPos( ImVec2( left + descriptionWidth + Px( 8 ), centerY - Px( 15 ) ) );
                        ProjectField( "project", m_edit->m_project, projectWidth, m_projects );

                        ImGui::SetCursorScreenPos( ImVec2( left + descriptionWidth + Px( projectWidth + 16 ), centerY - ImGui::GetFrameHeight() / 2 ) );
                        bool const save = AccentButton( "Save" );
                        ImGui::SetItemTooltip( "%s", pieces.size() > 1 ? std::format( "Rename all {} pieces", pieces.size() ).c_str() : "Save" );
                        ImGui::SameLine();
                        bool const cancel = ImGui::Button( "Cancel" );

                        if( save || enter )
                        {
                            std::string const description = Core::Trim( m_edit->m_description );
                            std::string const project = Core::Trim( m_edit->m_project );
                            action = [this, pieces, description, project]()
                            {
                                m_store.RewriteEntries( {}, pieces, description, project );
                                m_edit.reset();
                                Reload( true );
                            };
                        }
                        else if( cancel )
                        {
                            m_edit.reset();
                        }
                        break;
                    }

                    // Right side, fixed action slot first so hovering doesn't shift the times
                    float right = max.x - Px( 14 );
                    if( hot )
                    {
                        ImGui::SetCursorScreenPos( ImVec2( right - Px( 28 ), centerY - Px( 14 ) ) );
                        if( IconButton( "delete", Icon::Delete, pieces.size() > 1 ? "Delete all pieces" : "Delete" ) )
                        {
                            action = [this, pieces]() { Delete( pieces ); };
                        }

                        ImGui::SetCursorScreenPos( ImVec2( right - Px( 56 ), centerY - Px( 14 ) ) );
                        if( IconButton( "continue", Icon::Play, "Continue this" ) )
                        {
                            GroupKey const key = group.m_key;
                            action = [this, key]() { Start( key.m_description, key.m_project ); };
                        }
                    }
                    right -= Px( g_actionSlot );

                    std::string const total = Core::FormatHMS( group.m_total );
                    DrawText( pDrawList, ImVec2( right, centerY ), total, 14.5f, g_text, true, 1.0f );
                    right -= TextWidth( total, 14.5f, true ) + Px( 14 );
                    if( right - pos.x > Px( 320 ) )
                    {
                        std::string const range = Core::ClockHM( group.m_first ) + " – " + Core::ClockHM( group.m_last );
                        DrawText( pDrawList, ImVec2( right, centerY ), range, g_bodySize, g_muted, false, 1.0f );
                        right -= TextWidth( range ) + Px( 14 );
                    }

                    // Left side: piece count, dot, name, project, AUTO tag
                    float x = pos.x + Px( 14 );
                    if( group.m_items.size() > 1 )
                    {
                        ImVec2 const pillMin( x, centerY - Px( 11 ) );
                        ImVec2 const pillMax( x + Px( 36 ), centerY + Px( 11 ) );
                        ImGui::SetCursorScreenPos( pillMin );
                        if( ImGui::InvisibleButton( "pieces", ImVec2( pillMax.x - pillMin.x, pillMax.y - pillMin.y ) ) )
                        {
                            GroupKey const key = group.m_key;
                            action = [this, key]()
                            {
                                if( m_expanded.erase( key ) == 0 )
                                {
                                    m_expanded.insert( key );
                                }
                            };
                        }
                        ImGui::SetItemTooltip( open ? "Hide pieces" : "Show the separate pieces" );
                        pDrawList->AddRectFilled( pillMin, pillMax, ImGui::IsItemHovered() ? g_hover : g_card, Px( 10 ) );
                        pDrawList->AddRect( pillMin, pillMax, g_line, Px( 10 ) );
                        std::string const count = std::format( "{} {}", group.m_items.size(), open ? "▴" : "▾" );
                        DrawText( pDrawList, ImVec2( x + Px( 18 ), centerY ), count, 12.0f, open ? g_accent : g_muted, false, 0.5f );
                        x += Px( 44 );
                    }

                    pDrawList->AddCircleFilled( ImVec2( x + Px( 5 ), centerY ), Px( 4.5f ), ColorFor( group.m_key.m_project ) );
                    x += Px( 18 );

                    bool const hasName = !group.m_key.m_description.empty();
                    std::string const name = Ellipsize( hasName ? group.m_key.m_description : "Add description", std::max( right - x, Px( 20 ) ) );
                    float const nameWidth = TextWidth( name );
                    ImGui::SetCursorScreenPos( ImVec2( x, centerY - Px( 11 ) ) );
                    if( ImGui::InvisibleButton( "name", ImVec2( std::max( nameWidth, 1.0f ), Px( 22 ) ) ) )
                    {
                        m_edit = Edit{ group.m_key, group.m_key.m_description, group.m_key.m_project };
                    }

                    if( ImGui::IsItemHovered() )
                    {
                        ImGui::SetMouseCursor( ImGuiMouseCursor_TextInput );
                    }
                    ImGui::SetItemTooltip( "Click to rename or set a project" );
                    DrawText( pDrawList, ImVec2( x, centerY ), name, g_bodySize, hasName ? g_text : g_muted );
                    x += nameWidth + Px( 8 );

                    if( !group.m_key.m_project.empty() && right - x > Px( 120 ) )
                    {
                        std::string const project = Ellipsize( group.m_key.m_project, right - x - Px( 50 ) );
                        DrawText( pDrawList, ImVec2( x, centerY ), project, g_bodySize, ColorFor( group.m_key.m_project ) );
                        x += TextWidth( project ) + Px( 8 );
                    }

                    if( group.m_isAuto && right - x > Px( 60 ) )
                    {
                        DrawText( pDrawList, ImVec2( x, centerY ), "AUTO", 10.5f, Fade( g_accent, 0.8f ), true );
                        if( IsHovered( ImVec2( x, centerY - Px( 8 ) ), ImVec2( x + TextWidth( "AUTO", 10.5f, true ), centerY + Px( 8 ) ) ) )
                        {
                            ImGui::SetTooltip( "Tracked by a program rule" );
                        }
                    }
                }
                break;

                case RowKind::Item:
                {
                    Core::Entry const& entry = m_entries[row.m_index];
                    ImVec2 const max( pos.x + width, pos.y + Px( g_itemHeight ) );
                    float const centerY = pos.y + Px( g_itemHeight ) / 2;
                    bool const hot = IsHovered( pos, max );
                    pDrawList->AddRectFilled( pos, max, hot ? g_hover : g_bar, Px( 6 ) );

                    float right = max.x - Px( 14 );
                    if( hot )
                    {
                        ImGui::SetCursorScreenPos( ImVec2( right - Px( 28 ), centerY - Px( 14 ) ) );
                        if( IconButton( "delete", Icon::Delete, "Delete this piece" ) )
                        {
                            Core::Entry const piece = entry;
                            action = [this, piece]() { Delete( { piece } ); };
                        }
                    }
                    right -= Px( g_actionSlot );

                    std::string const length = Core::FormatHMS( entry.m_end - entry.m_start );
                    DrawText( pDrawList, ImVec2( right, centerY ), length, 13.5f, g_text, false, 1.0f );
                    right -= TextWidth( length, 13.5f ) + Px( 8 );
                    DrawText( pDrawList, ImVec2( right, centerY ), Core::ClockHM( entry.m_start ) + " – " + Core::ClockHM( entry.m_end ), 13.5f, g_muted, false, 1.0f );
                }
                break;
            }
            ImGui::PopID();
        }

        // Claim the full height, plus room for the undo toast
        ImGui::SetCursorScreenPos( origin );
        ImGui::Dummy( ImVec2( width, y + Px( 40 ) ) );
        ImGui::EndChild();

        if( action )
        {
            action();
        }
    }
}
