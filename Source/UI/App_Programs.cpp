#include "UI/App.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <format>

#include "Core/Text.h"
#include "UI/Widgets.h"

//-------------------------------------------------------------------------
// Programs page: add programs to track and edit the rules in place. Tracker options are on the Settings page.
//-------------------------------------------------------------------------

namespace Kui::UI
{
    void App::DrawPrograms()
    {
        bool changed = false;
        std::optional<std::string> add;
        std::optional<uint64_t> const runningRule = m_timer.has_value() ? m_timer->m_rule : std::nullopt;

        ImGui::BeginChild( "programs" );
        ImGui::BeginChild( "programsBody", ImVec2( std::min( ImGui::GetContentRegionAvail().x, Px( 800 ) ), 0 ), ImGuiChildFlags_AutoResizeY );
        Label( "Programs", 22.0f, g_text, true );
        Label( "Time is tracked automatically while one of these is in front.", g_bodySize, g_muted );
        ImGui::Dummy( ImVec2( 0, Px( 6 ) ) );

        // Add: click an open program, or type a name
        auto const isNew = [this]( std::string const& app )
        {
            return std::ranges::none_of( m_settings.m_rules, [&app]( Core::Rule const& r ) { return Core::IEqualsAscii( Core::Trim( r.m_keyword ), app ); } );
        };

        std::vector<std::string> open;
        std::ranges::copy_if( m_openPrograms, std::back_inserter( open ), isNew );
        std::vector<std::string> recent;
        std::ranges::copy_if( m_recent, std::back_inserter( recent ), [&]( std::string const& app ) { return isNew( app ) && std::ranges::find( m_openPrograms, app ) == m_openPrograms.end(); } );

        BeginCard( "add" );
        Caption( "ADD A PROGRAM" );
        Label( "Open right now — click to track", 13.0f, g_muted );
        if( std::optional<std::string> const clicked = Chips( "open", open, "Nothing new is open." ) )
        {
            add = clicked;
        }

        Label( "Recently seen", 13.0f, g_muted );
        if( std::optional<std::string> const clicked = Chips( "recent", recent, "Programs you use will show up here." ) )
        {
            add = clicked;
        }

        ImGui::Dummy( ImVec2( 0, 0 ) );
        ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - Px( 90 ) );
        ImGui::InputTextWithHint( "##manual", "Or type a program or window title word, e.g. youtube", &m_manualProgram );
        bool const enter = ImGui::IsItemDeactivated() && ( ImGui::IsKeyPressed( ImGuiKey_Enter ) || ImGui::IsKeyPressed( ImGuiKey_KeypadEnter ) );
        ImGui::SameLine( 0, Px( 10 ) );
        bool const canAdd = !Core::Trim( m_manualProgram ).empty();
        if( ( AccentButton( "Add", ImVec2( 80, 0 ), canAdd ) || enter ) && canAdd )
        {
            add = std::exchange( m_manualProgram, {} );
        }
        EndCard();

        // Tracked programs, edited in place
        ImGui::Dummy( ImVec2( 0, Px( 10 ) ) );
        Caption( "TRACKED PROGRAMS" );
        ImGui::SameLine();
        Label( std::to_string( m_settings.m_rules.size() ), 11.5f, g_muted );
        if( m_settings.m_rules.empty() )
        {
            Label( "None yet. Add one above and it will be timed whenever it's in front.", g_bodySize, g_muted );
        }

        std::optional<size_t> remove;
        std::optional<size_t> swap;     // Swaps rule i with i + 1
        size_t const count = m_settings.m_rules.size();
        float const buttonSize = ImGui::GetFrameHeight();
        float const spacing = ImGui::GetStyle().ItemSpacing.x;
        for( size_t i = 0; i < count; i++ )
        {
            Core::Rule& rule = m_settings.m_rules[i];
            bool const active = runningRule == rule.m_id;
            ImGui::PushID( static_cast<int>( rule.m_id ) );
            BeginCard( "rule", active ? g_accent : g_line, 12.0f, 10.0f );

            float const rowLeft = ImGui::GetCursorScreenPos().x;
            float const rowRight = rowLeft + ImGui::GetContentRegionAvail().x;
            float const rowY = ImGui::GetCursorScreenPos().y;
            changed |= ImGui::Checkbox( "##enabled", &rule.m_enabled );
            ImGui::SetItemTooltip( "Track this program" );
            ImGui::SameLine();
            Label( Core::Trim( rule.m_keyword ), 15.5f, rule.m_enabled ? g_text : g_muted, true );
            if( active )
            {
                ImGui::SameLine();
                Label( "● TRACKING", 11.0f, g_accent, true );
            }

            // Right side, laid out from the right edge: remove, down, up, match mode
            float x = rowRight - buttonSize;
            ImGui::SameLine();
            ImGui::SetCursorScreenPos( ImVec2( x, rowY ) );
            if( ImGui::Button( "✕", ImVec2( buttonSize, buttonSize ) ) )
            {
                remove = i;
            }
            ImGui::SetItemTooltip( "Remove program" );

            // First match wins, so order matters
            x -= buttonSize + spacing;
            ImGui::SameLine();
            ImGui::SetCursorScreenPos( ImVec2( x, rowY ) );
            ImGui::BeginDisabled( i + 1 >= count );
            if( ImGui::Button( "↓", ImVec2( buttonSize, buttonSize ) ) )
            {
                swap = i;
            }
            ImGui::SetItemTooltip( "Lower priority" );
            ImGui::EndDisabled();

            x -= buttonSize + spacing;
            ImGui::SameLine();
            ImGui::SetCursorScreenPos( ImVec2( x, rowY ) );
            ImGui::BeginDisabled( i == 0 );
            if( ImGui::Button( "↑", ImVec2( buttonSize, buttonSize ) ) )
            {
                swap = i - 1;
            }
            ImGui::SetItemTooltip( "Higher priority (first match wins)" );
            ImGui::EndDisabled();

            // Only multi-word keywords have a matching choice worth showing
            if( Core::SplitWhitespace( rule.m_keyword ).size() > 1 )
            {
                float const comboWidth = Px( 130 );
                x -= comboWidth + spacing;
                ImGui::SameLine();
                ImGui::SetCursorScreenPos( ImVec2( x, rowY ) );
                ImGui::SetNextItemWidth( comboWidth );
                if( ImGui::BeginCombo( "##mode", rule.m_mode == Core::Mode::Phrase ? "Exact phrase" : "Any order" ) )
                {
                    if( ImGui::Selectable( "Exact phrase", rule.m_mode == Core::Mode::Phrase ) )
                    {
                        rule.m_mode = Core::Mode::Phrase;
                        changed = true;
                    }

                    if( ImGui::Selectable( "All words, any order", rule.m_mode == Core::Mode::AllWords ) )
                    {
                        rule.m_mode = Core::Mode::AllWords;
                        changed = true;
                    }
                    ImGui::EndCombo();
                }
            }

            float const half = ( ImGui::GetContentRegionAvail().x - Px( 14 ) ) / 2;
            ImGui::SetNextItemWidth( half );
            std::string const hint = "Entry name (default: " + Core::Trim( rule.m_keyword ) + ")";
            changed |= ImGui::InputTextWithHint( "##description", hint.c_str(), &rule.m_description );
            ImGui::SameLine( 0, Px( 14 ) );
            changed |= ProjectField( "project", rule.m_project, half / Px( 1 ), m_projects );

            EndCard();
            ImGui::PopID();
        }

        if( remove.has_value() )
        {
            m_settings.m_rules.erase( m_settings.m_rules.begin() + static_cast<ptrdiff_t>( *remove ) );
            changed = true;
        }
        else if( swap.has_value() )
        {
            std::swap( m_settings.m_rules[*swap], m_settings.m_rules[*swap + 1] );
            changed = true;
        }

        ImGui::EndChild();
        ImGui::EndChild();

        if( add.has_value() )
        {
            // One click adds a program; its name and project can be edited on its card afterwards
            AddRule( *add, "", "" );
            changed = true;
        }

        if( changed )
        {
            Save();
        }
    }
}
