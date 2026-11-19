#include "UI/App.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>

#include "Core/Text.h"
#include "Platform/Platform.h"
#include "UI/Widgets.h"

//-------------------------------------------------------------------------
// The mini timer (`kui.exe --mini`): a small always-on-top window with just the timer.
// Main.cpp makes the grip strip on the left drag the borderless window.
//-------------------------------------------------------------------------

namespace Kui::UI
{
    void App::DrawMini()
    {
        bool const running = m_timer.has_value() && !m_timer->IsIdle();
        std::string& description = running ? m_timer->m_description : m_description;
        std::string& project = running ? m_timer->m_project : m_project;

        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        ImVec2 const origin = ImGui::GetWindowPos();
        ImVec2 const size = ImGui::GetWindowSize();
        float const midY = origin.y + size.y / 2;
        float const button = Px( 28 );
        bool changed = false;
        bool toggle = false;

        // Grip: 2x3 dots
        for( int i = 0; i < 6; i++ )
        {
            ImVec2 const dot( origin.x + Px( i % 2 == 0 ? 10.5f : 15.5f ), midY + Px( static_cast<float>( i / 2 - 1 ) * 5.0f ) );
            pDrawList->AddCircleFilled( dot, Px( 1.5f ), g_muted );
        }

        // Right to left: expand, pin, start/stop, clock
        float x = origin.x + size.x - Px( 10 ) - button;
        ImGui::SetCursorScreenPos( ImVec2( x, midY - button / 2 ) );
        if( IconButton( "##expand", Icon::Expand, "Open Kui" ) )
        {
            Platform::OpenApp();
        }

        x -= button;
        ImGui::SetCursorScreenPos( ImVec2( x, midY - button / 2 ) );
        if( IconButton( "##pin", Icon::Pin, m_miniPinned ? "On top. Click to unpin." : "Click to keep on top", m_miniPinned ) )
        {
            m_miniPinned = !m_miniPinned;
            HWND const hWnd = static_cast<HWND>( ImGui::GetMainViewport()->PlatformHandleRaw );
            SetWindowPos( hWnd, m_miniPinned ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE );
        }

        x -= Px( 46 ) + Px( 6 );
        ImGui::SetCursorScreenPos( ImVec2( x, midY - Px( 23 ) ) );
        toggle |= PlayButton( running );

        std::string const clock = Core::FormatHMS( LiveSecs() );
        x -= Px( 12 );
        TextAt( pDrawList, ImVec2( x, midY ), clock, 20.0f, running ? g_text : g_muted, true, 1.0f );
        float const right = x - TextWidth( clock, 20.0f, true ) - Px( 12 );

        // Left column: description over project
        float const left = origin.x + Px( 26 );
        float const columnWidth = std::max( right - left, Px( 80 ) );
        ImGui::SetCursorScreenPos( ImVec2( left, origin.y + Px( 9 ) ) );
        ImGui::PushFont( GetSemiboldFont(), 15.0f );
        ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 0, 0, 0, 0 ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 0, 0 ) );
        ImGui::SetNextItemWidth( columnWidth );
        changed |= ImGui::InputTextWithHint( "##description", "What are you working on?", &description );
        ImGui::PopStyleVar( 2 );
        ImGui::PopStyleColor();
        ImGui::PopFont();
        toggle |= !running && ImGui::IsItemDeactivated() && ( ImGui::IsKeyPressed( ImGuiKey_Enter ) || ImGui::IsKeyPressed( ImGuiKey_KeypadEnter ) );

        ImGui::SetCursorScreenPos( ImVec2( left, origin.y + Px( 35 ) ) );
        changed |= ProjectField( "project", project, std::min( columnWidth / Px( 1 ), 180.0f ), m_projects );
        if( running && changed )
        {
            m_store.SaveTimer( *m_timer );
        }

        if( toggle )
        {
            Toggle();
        }
    }
}
