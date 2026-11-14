#pragma once

#include <imgui.h>

#include <cstdint>
#include <map>
#include <string>
#include <string_view>

//-------------------------------------------------------------------------
// Colors, fonts and the ImGui style. Sizes in the UI code are logical pixels; Px() scales them for the monitor.
//-------------------------------------------------------------------------

namespace Kui::UI
{
    constexpr ImU32 g_side = IM_COL32( 13, 16, 20, 255 );
    constexpr ImU32 g_background = IM_COL32( 20, 24, 29, 255 );
    constexpr ImU32 g_bar = IM_COL32( 16, 19, 23, 255 );
    constexpr ImU32 g_field = IM_COL32( 11, 13, 16, 255 );
    constexpr ImU32 g_card = IM_COL32( 28, 33, 39, 255 );
    constexpr ImU32 g_hover = IM_COL32( 36, 42, 50, 255 );
    constexpr ImU32 g_line = IM_COL32( 44, 51, 59, 255 );
    constexpr ImU32 g_text = IM_COL32( 232, 236, 240, 255 );
    constexpr ImU32 g_muted = IM_COL32( 132, 142, 153, 255 );
    constexpr ImU32 g_accent = IM_COL32( 64, 200, 178, 255 );
    constexpr ImU32 g_accentDark = IM_COL32( 22, 60, 57, 255 );
    constexpr ImU32 g_green = IM_COL32( 110, 200, 140, 255 );
    constexpr ImU32 g_red = IM_COL32( 240, 112, 112, 255 );

    // Project colors, picked by a hash of the project name
    constexpr ImU32 g_palette[] =
    {
        IM_COL32( 64, 200, 178, 255 ),
        IM_COL32( 110, 160, 245, 255 ),
        IM_COL32( 245, 185, 80, 255 ),
        IM_COL32( 240, 112, 112, 255 ),
        IM_COL32( 170, 140, 240, 255 ),
        IM_COL32( 130, 205, 110, 255 ),
        IM_COL32( 240, 140, 200, 255 ),
        IM_COL32( 90, 200, 230, 255 ),
    };

    constexpr float g_hourHeight = 64.0f;   // Calendar pixels per hour
    constexpr float g_gutter = 60.0f;       // Calendar hour-label column
    constexpr float g_bodySize = 14.5f;     // Default text size
    constexpr float g_minTextScale = 0.8f;  // Text size setting range
    constexpr float g_maxTextScale = 1.6f;

    //-------------------------------------------------------------------------

    // Segoe UI regular and semibold, with Segoe UI Symbol merged in for arrows and marks
    void LoadFonts();

    // The dark theme, scaled for the monitor's DPI. Call again when the DPI changes. Keeps FontScaleMain (the text size setting).
    void ApplyStyle( float dpiScale );

    ImFont* GetRegularFont();
    ImFont* GetSemiboldFont();

    // Logical pixels to screen pixels
    inline float Px( float v ) { return v * ImGui::GetStyle().FontScaleDpi; }

    // Logical text size to screen pixels (includes the text size setting)
    inline float FontPx( float size ) { return Px( size ) * ImGui::GetStyle().FontScaleMain; }

    // Same color, alpha multiplied by `alpha`
    ImU32 Fade( ImU32 color, float alpha );

    // Brighter (factor > 1) or darker color
    ImU32 Scale( ImU32 color, float factor );

    // A stable palette color per project, or the user's pick; muted for "no project"
    ImU32 ColorFor( std::string_view project );

    // User-picked project colors that ColorFor checks first. Must outlive every ColorFor call.
    void SetProjectColors( std::map<std::string, uint32_t> const* pColors );
}
