#include "UI/App.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <format>

#include "Platform/Platform.h"
#include "UI/Widgets.h"

//-------------------------------------------------------------------------
// Settings popup: updates, startup, text size, the tracker options and CSV export. Saved as they change.
//-------------------------------------------------------------------------

namespace Kui::UI
{
    void App::DrawSettings()
    {
        if( std::exchange( m_openSettings, false ) )
        {
            ImGui::OpenPopup( "Settings" );
        }

        // Centered, dims the window behind
        ImGuiViewport const* pViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos( pViewport->GetCenter(), ImGuiCond_Always, ImVec2( 0.5f, 0.5f ) );
        ImGui::SetNextWindowSize( ImVec2( Px( 440 ), 0 ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( Px( 22 ), Px( 18 ) ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, Px( 12 ) );
        ImGui::PushStyleVar( ImGuiStyleVar_WindowBorderSize, 1.0f );
        m_settingsOpen = ImGui::BeginPopupModal( "Settings", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings );
        ImGui::PopStyleVar( 3 );
        if( !m_settingsOpen )
        {
            return;
        }

        bool changed = false;

        // Sections split by thin lines instead of cards, to stay compact
        auto const section = []( char const* pTitle )
        {
            ImGui::Dummy( ImVec2( 0, Px( 2 ) ) );
            ImGui::Separator();
            ImGui::Dummy( ImVec2( 0, Px( 2 ) ) );
            Caption( pTitle );
        };

        // Title row with a close button on the right
        float const closeSize = ImGui::GetFrameHeight();
        float const rowY = ImGui::GetCursorPosY();
        Label( "Settings", 17.0f, g_text, true );
        Label( "Changes are saved right away.", 12.5f, g_muted );
        float const afterTitle = ImGui::GetCursorPosY();
        ImGui::SetCursorPos( ImVec2( ImGui::GetWindowContentRegionMax().x - closeSize, rowY ) );
        bool close = ImGui::Button( "✕", ImVec2( closeSize, closeSize ) );
        ImGui::SetItemTooltip( "Close (Esc)" );
        ImGui::SetCursorPosY( afterTitle );
        ImGui::Dummy( ImVec2( 0, Px( 2 ) ) );
        Caption( "UPDATES" );
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

        section( "GENERAL" );
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

        section( "AUTO-TRACKING" );
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

        // Status shown here, since toasts sit behind the popup
        section( "EXPORT" );
        ImGui::BeginDisabled( m_isScreenshot );
        if( ImGui::Button( "Export CSV" ) )
        {
            std::optional<std::filesystem::path> const path = m_store.ExportCsv();
            m_exportStatus = path.has_value() ? "Saved time-entries.csv" : "Couldn't write the CSV file";
            if( path.has_value() )
            {
                Platform::RevealInExplorer( *path );
            }
        }
        ImGui::EndDisabled();
        ImGui::SetItemTooltip( "Save every entry as a spreadsheet file and show it" );
        ImGui::SameLine();
        RowLabel( m_exportStatus.empty() ? "Every entry, as a spreadsheet file" : m_exportStatus, g_bodySize, g_muted );

        // Esc or a click on the dimmed area closes it
        close |= ImGui::IsKeyPressed( ImGuiKey_Escape );
        close |= ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && !ImGui::IsWindowHovered( ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem );
        if( close )
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();

        if( changed )
        {
            Save();
        }
    }
}
