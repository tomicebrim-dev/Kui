#pragma once

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "UI/Theme.h"

//-------------------------------------------------------------------------
// Small painted widgets shared by the views. All sizes are logical pixels.
//-------------------------------------------------------------------------

namespace Kui::UI
{
    enum class Icon
    {
        Play,
        Delete,
    };

    //-------------------------------------------------------------------------

    // Text in a given size, color and weight
    void Label( std::string_view text, float size = g_bodySize, ImU32 color = g_text, bool semibold = false );

    // Small semibold all-caps section title
    void Caption( std::string_view text );

    // 10px colored dot, laid out like a widget
    void Dot( ImU32 color );

    // Width of a text in screen pixels
    float TextWidth( std::string_view text, float size = g_bodySize, bool semibold = false );

    // Cuts the text to fit `maxWidth` screen pixels, ending in an ellipsis
    std::string Ellipsize( std::string_view text, float maxWidth, float size = g_bodySize, bool semibold = false );

    // Paints text vertically centered on pos.y. alignX: 0 = left of pos, 0.5 = centered, 1 = right of pos.
    // A maxWidth > 0 truncates with an ellipsis.
    void DrawText( ImDrawList* pDrawList, ImVec2 pos, std::string_view text, float size, ImU32 color, bool semibold = false, float alignX = 0.0f, float maxWidth = 0.0f );

    // A padded, rounded, auto-height panel. Always pair with EndCard().
    void BeginCard( char const* pId, ImU32 border = g_line, float padding = 18.0f, float rounding = 12.0f, ImU32 fill = g_card, float width = 0.0f );
    void EndCard();

    // Text-only button: brightens on hover
    bool FlatButton( char const* pId, std::string_view text, float size = 12.5f, ImU32 color = g_muted, bool semibold = false );

    // Filled accent button with dark semibold text
    bool AccentButton( char const* pLabel, ImVec2 minSize = ImVec2( 0, 0 ), bool enabled = true );

    // Sidebar row: accent bar and tint when active, shortcut shown on hover
    bool NavItem( char const* pLabel, char const* pShortcut, bool active );

    // Project text box with a colored dot and a list of known projects. True when changed.
    bool ProjectField( char const* pId, std::string& text, float width, std::vector<std::string> const& projects );

    // The big round start/stop button
    bool PlayButton( bool running );

    // 28px round icon button, painted (no glyph-font dependency)
    bool IconButton( char const* pId, Icon icon, char const* pTooltip );

    // Wrapped row of "+ name" pills. Returns the clicked name.
    std::optional<std::string> Chips( char const* pId, std::vector<std::string> const& names, char const* pEmptyText );

    // Flow layout: puts the next item of `width` on the current line if it fits before `rightEdge`
    void SameLineIfFits( float width, float rightEdge, float spacing );
}
