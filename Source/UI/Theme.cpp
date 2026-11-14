#include "UI/Theme.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include "Core/Text.h"

//-------------------------------------------------------------------------

namespace Kui::UI
{
    static std::map<std::string, uint32_t> const* g_pProjectColors = nullptr;

    static ImVec4 Vec( ImU32 color )
    {
        return ImGui::ColorConvertU32ToFloat4( color );
    }

    // Adds a memory-mapped font file to the atlas. File-backed pages are shared with the OS cache instead of
    // ~7 MB of private heap copies. Never unmapped: ImGui reads glyphs on demand for the process lifetime.
    static ImFont* AddMappedFont( std::filesystem::path const& path, bool merge )
    {
        HANDLE const file = CreateFileW( path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
        if( file == INVALID_HANDLE_VALUE )
        {
            return nullptr;
        }

        LARGE_INTEGER size = {};
        GetFileSizeEx( file, &size );
        HANDLE const mapping = CreateFileMappingW( file, nullptr, PAGE_READONLY, 0, 0, nullptr );
        CloseHandle( file );
        if( mapping == nullptr )
        {
            return nullptr;
        }

        void* const pData = MapViewOfFile( mapping, FILE_MAP_READ, 0, 0, 0 );
        CloseHandle( mapping ); // The view keeps the mapping alive
        if( pData == nullptr )
        {
            return nullptr;
        }

        ImFontConfig config;
        config.FontDataOwnedByAtlas = false;
        config.MergeMode = merge;
        return ImGui::GetIO().Fonts->AddFontFromMemoryTTF( pData, static_cast<int>( size.QuadPart ), 0.0f, &config );
    }

    // Adds a font plus the symbol fallback. Returns nullptr if the font file is missing.
    static ImFont* AddFont( std::filesystem::path const& fonts, char const* pFile )
    {
        ImFont* const pFont = AddMappedFont( fonts / pFile, false );
        if( pFont != nullptr )
        {
            AddMappedFont( fonts / "seguisym.ttf", true );
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
        float const fontScale = style.FontScaleMain;
        style = ImGuiStyle();
        style.FontSizeBase = g_bodySize;
        style.WindowPadding = ImVec2( 10, 7 );      // Tooltips and popups; the root window pushes 0
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
        style.FontScaleMain = fontScale;

        ImVec4* pColors = style.Colors;
        pColors[ImGuiCol_Text] = Vec( g_text );
        pColors[ImGuiCol_TextDisabled] = Vec( g_muted );
        pColors[ImGuiCol_WindowBg] = Vec( g_background );
        pColors[ImGuiCol_ChildBg] = ImVec4( 0, 0, 0, 0 );
        pColors[ImGuiCol_PopupBg] = Vec( g_card );
        pColors[ImGuiCol_Border] = Vec( g_line );
        pColors[ImGuiCol_FrameBg] = Vec( g_field );
        pColors[ImGuiCol_FrameBgHovered] = Vec( g_field );
        pColors[ImGuiCol_FrameBgActive] = Vec( g_field );
        pColors[ImGuiCol_Button] = Vec( g_card );
        pColors[ImGuiCol_ButtonHovered] = Vec( g_hover );
        pColors[ImGuiCol_ButtonActive] = Vec( g_accentDark );
        pColors[ImGuiCol_Header] = Vec( g_accentDark );
        pColors[ImGuiCol_HeaderHovered] = Vec( g_hover );
        pColors[ImGuiCol_HeaderActive] = Vec( g_accentDark );
        pColors[ImGuiCol_CheckMark] = Vec( g_accent );
        pColors[ImGuiCol_CheckboxSelectedBg] = Vec( g_accentDark );
        pColors[ImGuiCol_SliderGrab] = Vec( g_accent );
        pColors[ImGuiCol_SliderGrabActive] = Vec( g_accent );
        pColors[ImGuiCol_TextSelectedBg] = Vec( g_accentDark );
        pColors[ImGuiCol_InputTextCursor] = Vec( g_accent );
        pColors[ImGuiCol_ScrollbarBg] = ImVec4( 0, 0, 0, 0 );
        pColors[ImGuiCol_ScrollbarGrab] = Vec( g_line );
        pColors[ImGuiCol_ScrollbarGrabHovered] = Vec( g_muted );
        pColors[ImGuiCol_ScrollbarGrabActive] = Vec( g_muted );
        pColors[ImGuiCol_Separator] = Vec( g_line );
        pColors[ImGuiCol_ModalWindowDimBg] = ImVec4( 0, 0, 0, 0.55f );
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

        if( g_pProjectColors != nullptr )
        {
            auto const found = g_pProjectColors->find( std::string( Core::Trim( project ) ) );
            if( found != g_pProjectColors->end() )
            {
                return found->second | IM_COL32_A_MASK;
            }
        }

        // Same hash as the original, so projects keep their colors
        size_t hash = 5;
        for( char c : project )
        {
            hash = hash * 131 + static_cast<unsigned char>( c );
        }
        return g_palette[hash % std::size( g_palette )];
    }

    void SetProjectColors( std::map<std::string, uint32_t> const* pColors )
    {
        g_pProjectColors = pColors;
    }
}
