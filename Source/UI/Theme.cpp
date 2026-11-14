#include "UI/Theme.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include "Core/Text.h"

//-------------------------------------------------------------------------

namespace Kui::UI
{
    static ImVec4 Vec( ImU32 color )
    {
        return ImGui::ColorConvertU32ToFloat4( color );
    }

    // Adds a font plus the symbol fallback. Returns nullptr if the font file is missing.
    static ImFont* AddFont( std::filesystem::path const& fonts, char const* pFile )
    {
        ImGuiIO& io = ImGui::GetIO();
        std::filesystem::path const path = fonts / pFile;
        if( !std::filesystem::exists( path ) )
        {
            return nullptr;
        }

        ImFont* pFont = io.Fonts->AddFontFromFileTTF( Core::Narrow( path.wstring() ).c_str() );
        std::filesystem::path const symbols = fonts / "seguisym.ttf";
        if( std::filesystem::exists( symbols ) )
        {
            ImFontConfig config;
            config.MergeMode = true;
            io.Fonts->AddFontFromFileTTF( Core::Narrow( symbols.wstring() ).c_str(), 0.0f, &config );
        }
        return pFont;
    }

    //-------------------------------------------------------------------------

    void LoadFonts()
    {
        wchar_t windows[MAX_PATH];
        GetWindowsDirectoryW( windows, MAX_PATH );
        std::filesystem::path const fonts = std::filesystem::path( windows ) / "Fonts";

        // Index 0 = regular, 1 = semibold; the default font stands in for either if Segoe UI is missing
        ImGuiIO& io = ImGui::GetIO();
        if( AddFont( fonts, "segoeui.ttf" ) == nullptr )
        {
            io.Fonts->AddFontDefault();
        }

        if( AddFont( fonts, "seguisb.ttf" ) == nullptr )
        {
            io.Fonts->AddFontDefault();
        }
    }

    ImFont* GetRegularFont()
    {
        return ImGui::GetIO().Fonts->Fonts[0];
    }

    ImFont* GetSemiboldFont()
    {
        return ImGui::GetIO().Fonts->Fonts[1];
    }

    void ApplyStyle( float dpiScale )
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style = ImGuiStyle();
        style.FontSizeBase = g_bodySize;
        style.WindowPadding = ImVec2( 0, 0 );
        style.ItemSpacing = ImVec2( 8, 8 );
        style.ItemInnerSpacing = ImVec2( 6, 6 );
        style.FramePadding = ImVec2( 10, 6 );
        style.FrameRounding = 7;
        style.ChildRounding = 12;
        style.PopupRounding = 8;
        style.WindowRounding = 10;
        style.GrabRounding = 7;
        style.ScrollbarSize = 10;
        style.ScrollbarRounding = 8;
        style.FrameBorderSize = 1;
        style.ChildBorderSize = 1;
        style.PopupBorderSize = 1;
        style.WindowBorderSize = 0;
        style.PopupBorderSize = 1;
        style.ScaleAllSizes( dpiScale );
        style.FontScaleDpi = dpiScale;

        ImVec4* pColors = style.Colors;
        pColors[ImGuiCol_Text] = Vec( g_text );
        pColors[ImGuiCol_TextDisabled] = Vec( g_muted );
        pColors[ImGuiCol_WindowBg] = Vec( g_background );
        pColors[ImGuiCol_ChildBg] = ImVec4( 0, 0, 0, 0 );
        pColors[ImGuiCol_PopupBg] = Vec( g_card );
        pColors[ImGuiCol_Border] = Vec( g_line );
        pColors[ImGuiCol_FrameBg] = Vec( g_bar );
        pColors[ImGuiCol_FrameBgHovered] = Vec( g_bar );
        pColors[ImGuiCol_FrameBgActive] = Vec( g_bar );
        pColors[ImGuiCol_Button] = Vec( g_card );
        pColors[ImGuiCol_ButtonHovered] = Vec( g_hover );
        pColors[ImGuiCol_ButtonActive] = Vec( g_accentDark );
        pColors[ImGuiCol_Header] = Vec( g_accentDark );
        pColors[ImGuiCol_HeaderHovered] = Vec( g_hover );
        pColors[ImGuiCol_HeaderActive] = Vec( g_accentDark );
        pColors[ImGuiCol_CheckMark] = Vec( g_accent );
        pColors[ImGuiCol_SliderGrab] = Vec( g_accent );
        pColors[ImGuiCol_SliderGrabActive] = Vec( g_accent );
        pColors[ImGuiCol_TextSelectedBg] = Vec( g_accentDark );
        pColors[ImGuiCol_InputTextCursor] = Vec( g_accent );
        pColors[ImGuiCol_ScrollbarBg] = ImVec4( 0, 0, 0, 0 );
        pColors[ImGuiCol_ScrollbarGrab] = Vec( g_line );
        pColors[ImGuiCol_ScrollbarGrabHovered] = Vec( g_muted );
        pColors[ImGuiCol_ScrollbarGrabActive] = Vec( g_muted );
        pColors[ImGuiCol_Separator] = Vec( g_line );
    }

    ImU32 Fade( ImU32 color, float alpha )
    {
        ImVec4 c = Vec( color );
        c.w *= alpha;
        return ImGui::ColorConvertFloat4ToU32( c );
    }

    ImU32 Scale( ImU32 color, float factor )
    {
        ImVec4 c = Vec( color );
        c.x = std::min( c.x * factor, 1.0f );
        c.y = std::min( c.y * factor, 1.0f );
        c.z = std::min( c.z * factor, 1.0f );
        return ImGui::ColorConvertFloat4ToU32( c );
    }

    ImU32 ColorFor( std::string_view project )
    {
        if( Core::Trim( project ).empty() )
        {
            return g_muted;
        }

        // Same hash as the original, so projects keep their colors
        size_t hash = 5;
        for( char c : project )
        {
            hash = hash * 131 + static_cast<unsigned char>( c );
        }
        return g_palette[hash % std::size( g_palette )];
    }
}
