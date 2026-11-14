#include "UI/App.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>

#include "Platform/Platform.h"
#include "UI/Widgets.h"

//-------------------------------------------------------------------------
// Settings page: startup, text size, the tracker options and updates. Saved as they change.
//-------------------------------------------------------------------------

namespace Kui::UI
{
    void App::DrawSettings()
    {
        bool changed = false;

        ImGui::BeginChild( "settings" );
        ImGui::BeginChild( "settingsBody", ImVec2( std::min( ImGui::GetContentRegionAvail().x, Px( 800 ) ), 0 ), ImGuiChildFlags_AutoResizeY );
        Label( "Settings", 22.0f, g_text, true );
        Label( "Changes are saved right away.", g_bodySize, g_muted );
        ImGui::Dummy( ImVec2( 0, Px( 6 ) ) );

        // General
        Caption( "GENERAL" );
        BeginCard( "general" );
        if( ImGui::Checkbox( "Start with Windows", &m_settings.m_autostart ) && !m_isScreenshot )
        {
            Platform::SetAutostart( m_settings.m_autostart );
            changed = true;
        }

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted( "Text size" );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( Px( 180 ) );
        int percent = static_cast<int>( std::lround( m_settings.m_textScale * 100.0f ) );
        if( ImGui::SliderInt( "##textSize", &percent, static_cast<int>( g_minTextScale * 100 ), static_cast<int>( g_maxTextScale * 100 ), "%d%%", ImGuiSliderFlags_AlwaysClamp ) )
        {
            m_settings.m_textScale = static_cast<float>( percent ) / 100.0f;
            ImGui::GetStyle().FontScaleMain = m_settings.m_textScale;
        }

        // Save once the drag ends, not every frame of it
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        EndCard();

        // Tracking
        ImGui::Dummy( ImVec2( 0, Px( 10 ) ) );
        Caption( "AUTO-TRACKING" );
        BeginCard( "tracking" );
        changed |= ImGui::Checkbox( "Auto-tracking on", &m_settings.m_tracking );
        changed |= ImGui::Checkbox( "Stop when no program matches", &m_settings.m_stopOnNoMatch );
        changed |= ImGui::Checkbox( "Only after the program is in front for", &m_settings.m_focusOn );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( Px( 80 ) );
        changed |= ImGui::DragInt( "##focusSecs", &m_settings.m_focusSecs, 1.0f, 1, 300, "%d s", ImGuiSliderFlags_AlwaysClamp );
        changed |= ImGui::Checkbox( "Stop when I'm away for", &m_settings.m_idleOn );
        ImGui::SameLine();
        ImGui::SetNextItemWidth( Px( 80 ) );
        changed |= ImGui::DragInt( "##idleMinutes", &m_settings.m_idleMinutes, 1.0f, 1, 480, "%d min", ImGuiSliderFlags_AlwaysClamp );
        EndCard();

        // Updates
        ImGui::Dummy( ImVec2( 0, Px( 10 ) ) );
        Caption( "UPDATES" );
        BeginCard( "updates" );
        changed |= ImGui::Checkbox( "Check for updates when Kui opens", &m_settings.m_checkUpdates );
        Label( std::format( "Version {}", Updater::CurrentVersion() ), g_bodySize, g_muted );
        if( !m_updateStatus.empty() )
        {
            Label( m_updateStatus, g_bodySize, m_update.has_value() ? g_accent : g_muted );
        }

        bool const busy = m_updateCheck.valid() || m_updateInstall.valid();
        if( m_update.has_value() )
        {
            std::string const label = std::format( "Update to {}", m_update->m_version );
            if( AccentButton( label.c_str(), ImVec2( 0, 0 ), !busy ) )
            {
                InstallUpdate();
            }
            ImGui::SameLine();
        }

        ImGui::BeginDisabled( busy || m_isScreenshot );
        if( ImGui::Button( "Check now" ) )
        {
            CheckForUpdate();
        }
        ImGui::EndDisabled();
        EndCard();

        ImGui::EndChild();
        ImGui::EndChild();

        if( changed )
        {
            Save();
        }
    }
}
