#include "UI/Widgets.h"

#include <imgui_stdlib.h>

#include <cfloat>
#include <cmath>

#include "Core/Text.h"

//-------------------------------------------------------------------------

namespace Kui::UI
{
    static ImFont* FontFor( bool semibold )
    {
        return semibold ? GetSemiboldFont() : GetRegularFont();
    }

    //-------------------------------------------------------------------------

    void Label( std::string_view text, float size, ImU32 color, bool semibold )
    {
        ImGui::PushFont( FontFor( semibold ), size );
        ImGui::PushStyleColor( ImGuiCol_Text, color );
        ImGui::TextUnformatted( text.data(), text.data() + text.size() );
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }

    void RowLabel( std::string_view text, float size, ImU32 color, bool semibold )
    {
        ImVec2 const pos = ImGui::GetCursorScreenPos();
        float const baseline = pos.y + ImGui::GetStyle().FramePadding.y + ImGui::GetFontBaked()->Ascent;
        ImFont* const pFont = FontFor( semibold );
        float const px = FontPx( size );
        ImVec2 const topLeft( pos.x, baseline - pFont->GetFontBaked( px )->Ascent );
        ImGui::GetWindowDrawList()->AddText( pFont, px, topLeft, color, text.data(), text.data() + text.size() );
        ImGui::Dummy( ImVec2( TextWidth( text, size, semibold ), ImGui::GetFrameHeight() ) );
    }

    void Caption( std::string_view text )
    {
        Label( text, 11.5f, g_muted, true );
    }

    void Dot( ImU32 color )
    {
        ImVec2 const pos = ImGui::GetCursorScreenPos();
        float const height = ImGui::GetTextLineHeight();
        ImGui::Dummy( ImVec2( Px( 10 ), height ) );
        ImGui::GetWindowDrawList()->AddCircleFilled( ImVec2( pos.x + Px( 5 ), pos.y + height / 2 ), Px( 4.5f ), color );
    }

    float TextWidth( std::string_view text, float size, bool semibold )
    {
        return FontFor( semibold )->CalcTextSizeA( FontPx( size ), FLT_MAX, 0.0f, text.data(), text.data() + text.size() ).x;
    }

    std::string Ellipsize( std::string_view text, float maxWidth, float size, bool semibold )
    {
        if( TextWidth( text, size, semibold ) <= maxWidth )
        {
            return std::string( text );
        }

        // Drop whole UTF-8 characters from the end until "text…" fits
        std::string cut( text );
        while( !cut.empty() )
        {
            do
            {
                cut.pop_back();
            }
            while( !cut.empty() && ( static_cast<unsigned char>( cut.back() ) & 0xC0 ) == 0x80 );

            if( !cut.empty() && ( static_cast<unsigned char>( cut.back() ) & 0x80 ) != 0 )
            {
                cut.pop_back(); // Lead byte of the character just emptied
            }

            if( TextWidth( cut + "…", size, semibold ) <= maxWidth )
            {
                break;
            }
        }
        return cut + "…";
    }

    void TextAt( ImDrawList* pDrawList, ImVec2 pos, std::string_view text, float size, ImU32 color, bool semibold, float alignX, float maxWidth )
    {
        std::string const shown = maxWidth > 0 ? Ellipsize( text, maxWidth, size, semibold ) : std::string( text );
        float const px = FontPx( size );
        float const width = TextWidth( shown, size, semibold );
        ImVec2 const topLeft( pos.x - width * alignX, pos.y - px / 2 );
        pDrawList->AddText( FontFor( semibold ), px, topLeft, color, shown.c_str(), shown.c_str() + shown.size() );
    }

    void BeginCard( char const* pId, ImU32 border, float padding, float rounding, ImU32 fill, float width )
    {
        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( Px( padding ), Px( padding ) ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ChildRounding, Px( rounding ) );
        ImGui::PushStyleColor( ImGuiCol_ChildBg, fill );
        ImGui::PushStyleColor( ImGuiCol_Border, border );
        ImGuiChildFlags const flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding;
        ImGui::BeginChild( pId, ImVec2( width, 0 ), flags, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
        ImGui::PopStyleColor( 2 );
        ImGui::PopStyleVar( 2 );
    }

    void EndCard()
    {
        ImGui::EndChild();
    }

    bool FlatButton( char const* pId, std::string_view text, float size, ImU32 color, bool semibold )
    {
        ImVec2 const pos = ImGui::GetCursorScreenPos();
        float const height = ImGui::GetTextLineHeight();
        bool const clicked = ImGui::InvisibleButton( pId, ImVec2( TextWidth( text, size, semibold ), height ) );
        ImU32 const shown = ImGui::IsItemHovered() ? g_text : color;
        TextAt( ImGui::GetWindowDrawList(), ImVec2( pos.x, pos.y + height / 2 ), text, size, shown, semibold );
        return clicked;
    }

    bool AccentButton( char const* pLabel, ImVec2 minSize, bool enabled )
    {
        ImGui::PushStyleColor( ImGuiCol_Button, g_accent );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, Scale( g_accent, 1.1f ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, Scale( g_accent, 0.9f ) );
        ImGui::PushStyleColor( ImGuiCol_Border, g_accent );
        ImGui::PushStyleColor( ImGuiCol_Text, g_side );
        ImGui::PushFont( GetSemiboldFont(), g_bodySize );
        ImGui::BeginDisabled( !enabled );
        bool const clicked = ImGui::Button( pLabel, ImVec2( Px( minSize.x ), Px( minSize.y ) ) );
        ImGui::EndDisabled();
        ImGui::PopFont();
        ImGui::PopStyleColor( 5 );
        return clicked;
    }

    bool ProjectField( char const* pId, std::string& text, float width, std::vector<std::string> const& projects, std::map<std::string, uint32_t>* pColors )
    {
        ImGui::PushID( pId );
        bool changed = false;
        ImVec2 const pos = ImGui::GetCursorScreenPos();
        ImVec2 const size( Px( width ), Px( 30 ) );
        ImVec2 const max( pos.x + size.x, pos.y + size.y );
        float const centerY = pos.y + size.y / 2;
        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        pDrawList->AddRectFilled( pos, max, g_field, Px( 8 ) );
        pDrawList->AddRect( pos, max, g_line, Px( 8 ) );
        pDrawList->AddCircleFilled( ImVec2( pos.x + Px( 15 ), centerY ), Px( 4.5f ), ColorFor( text ) );

        // The dot picks the project's color
        std::string const project( Core::Trim( text ) );
        if( pColors != nullptr && !project.empty() )
        {
            ImGui::SetCursorScreenPos( ImVec2( pos.x + Px( 5 ), pos.y ) );
            if( ImGui::InvisibleButton( "##color", ImVec2( Px( 20 ), size.y ) ) )
            {
                ImGui::OpenPopup( "color" );
            }
            ImGui::SetItemTooltip( "Change the project's color" );
            if( ImGui::IsItemHovered() )
            {
                pDrawList->AddCircle( ImVec2( pos.x + Px( 15 ), centerY ), Px( 7 ), g_muted, 0, Px( 1.2f ) );
            }

            ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( Px( 10 ), Px( 10 ) ) );
            if( ImGui::BeginPopup( "color" ) )
            {
                for( size_t i = 0; i < std::size( g_palette ); i++ )
                {
                    ImGui::PushID( static_cast<int>( i ) );
                    if( i > 0 )
                    {
                        ImGui::SameLine();
                    }

                    if( ImGui::ColorButton( "##swatch", ImGui::ColorConvertU32ToFloat4( g_palette[i] ), ImGuiColorEditFlags_NoTooltip, ImVec2( Px( 22 ), Px( 22 ) ) ) )
                    {
                        ( *pColors )[project] = g_palette[i];
                        changed = true;
                    }
                    ImGui::PopID();
                }

                ImVec4 color = ImGui::ColorConvertU32ToFloat4( ColorFor( project ) );
                ImGui::SetNextItemWidth( Px( 230 ) );
                if( ImGui::ColorPicker3( "##picker", &color.x, ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_NoSmallPreview | ImGuiColorEditFlags_NoInputs ) )
                {
                    ( *pColors )[project] = ImGui::ColorConvertFloat4ToU32( color );
                    changed = true;
                }

                ImGui::BeginDisabled( !pColors->contains( project ) );
                if( ImGui::Button( "Reset to default" ) )
                {
                    pColors->erase( project );
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();
        }

        // Frameless text box between the dot and the arrow
        ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 0, 0, 0, 0 ) );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 0.0f );
        ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( Px( 2 ), ImGui::GetStyle().FramePadding.y ) );
        ImGui::SetCursorScreenPos( ImVec2( pos.x + Px( 24 ), centerY - ImGui::GetFrameHeight() / 2 ) );
        ImGui::SetNextItemWidth( std::max( size.x - Px( 54 ), Px( 30 ) ) );
        changed |= ImGui::InputTextWithHint( "##text", "Project", &text );
        ImGui::PopStyleVar( 2 );
        ImGui::PopStyleColor();

        ImGui::SetCursorScreenPos( ImVec2( max.x - Px( 26 ), pos.y ) );
        if( ImGui::InvisibleButton( "##pick", ImVec2( Px( 24 ), size.y ) ) )
        {
            ImGui::OpenPopup( "projects" );
        }
        ImGui::SetItemTooltip( "Pick a project" );
        TextAt( pDrawList, ImVec2( max.x - Px( 14 ), centerY ), "▾", 14.0f, ImGui::IsItemHovered() ? g_text : g_muted, false, 0.5f );

        ImGui::SetNextWindowSizeConstraints( ImVec2( Px( 180 ), 0 ), ImVec2( FLT_MAX, Px( 320 ) ) );
        if( ImGui::BeginPopup( "projects" ) )
        {
            if( projects.empty() )
            {
                Label( "No projects yet. Type one.", g_bodySize, g_muted );
            }

            for( std::string const& project : projects )
            {
                Dot( ColorFor( project ) );
                ImGui::SameLine();
                if( ImGui::Selectable( project.c_str() ) )
                {
                    text = project;
                    changed = true;
                }
            }

            if( !text.empty() )
            {
                ImGui::PushStyleColor( ImGuiCol_Text, g_muted );
                if( ImGui::Selectable( "No project" ) )
                {
                    text.clear();
                    changed = true;
                }
                ImGui::PopStyleColor();
            }
            ImGui::EndPopup();
        }

        // Claim the whole box in the layout
        ImGui::SetCursorScreenPos( pos );
        ImGui::Dummy( size );
        ImGui::PopID();
        return changed;
    }

    bool PlayButton( bool running )
    {
        ImVec2 const pos = ImGui::GetCursorScreenPos();
        float const size = Px( 46 );
        bool const clicked = ImGui::InvisibleButton( "##play", ImVec2( size, size ) );
        ImU32 const base = running ? g_red : g_accent;
        ImVec2 const c( pos.x + size / 2, pos.y + size / 2 );

        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        pDrawList->AddCircleFilled( c, size / 2, ImGui::IsItemHovered() ? Scale( base, 1.15f ) : base );
        if( running )
        {
            pDrawList->AddRectFilled( ImVec2( c.x - Px( 7 ), c.y - Px( 7 ) ), ImVec2( c.x + Px( 7 ), c.y + Px( 7 ) ), g_side, Px( 3 ) );
        }
        else
        {
            pDrawList->AddTriangleFilled( ImVec2( c.x - Px( 5 ), c.y - Px( 9 ) ), ImVec2( c.x + Px( 10 ), c.y ), ImVec2( c.x - Px( 5 ), c.y + Px( 9 ) ), g_side );
        }

        ImGui::SetItemTooltip( running ? "Stop timer (S)" : "Start timer (S)" );
        return clicked;
    }

    // Paints an icon centered on c, about 16px wide
    static void DrawIcon( ImDrawList* pDrawList, Icon icon, ImVec2 c, ImU32 color )
    {
        switch( icon )
        {
            case Icon::Play:
            {
                pDrawList->AddTriangleFilled( ImVec2( c.x - Px( 3.5f ), c.y - Px( 5.5f ) ), ImVec2( c.x + Px( 5.5f ), c.y ), ImVec2( c.x - Px( 3.5f ), c.y + Px( 5.5f ) ), color );
            }
            break;

            case Icon::Delete:
            {
                float const r = Px( 4.5f );
                pDrawList->AddLine( ImVec2( c.x - r, c.y - r ), ImVec2( c.x + r, c.y + r ), color, Px( 1.6f ) );
                pDrawList->AddLine( ImVec2( c.x + r, c.y - r ), ImVec2( c.x - r, c.y + r ), color, Px( 1.6f ) );
            }
            break;

            case Icon::Timer:
            {
                // Stopwatch: face, hand and crown
                ImVec2 const face( c.x, c.y + Px( 1.5f ) );
                pDrawList->AddCircle( face, Px( 7.5f ), color, 0, Px( 1.6f ) );
                pDrawList->AddLine( face, ImVec2( face.x, face.y - Px( 4 ) ), color, Px( 1.6f ) );
                pDrawList->AddLine( ImVec2( c.x - Px( 2.5f ), c.y - Px( 8.5f ) ), ImVec2( c.x + Px( 2.5f ), c.y - Px( 8.5f ) ), color, Px( 1.6f ) );
            }
            break;

            case Icon::Calendar:
            {
                // Page with a header line and two rings
                ImVec2 const min( c.x - Px( 8 ), c.y - Px( 6.5f ) );
                ImVec2 const max( c.x + Px( 8 ), c.y + Px( 8 ) );
                pDrawList->AddRect( min, max, color, Px( 2.5f ), 0, Px( 1.6f ) );
                pDrawList->AddLine( ImVec2( min.x, c.y - Px( 2 ) ), ImVec2( max.x, c.y - Px( 2 ) ), color, Px( 1.6f ) );
                pDrawList->AddLine( ImVec2( c.x - Px( 4 ), c.y - Px( 9 ) ), ImVec2( c.x - Px( 4 ), c.y - Px( 5 ) ), color, Px( 1.6f ) );
                pDrawList->AddLine( ImVec2( c.x + Px( 4 ), c.y - Px( 9 ) ), ImVec2( c.x + Px( 4 ), c.y - Px( 5 ) ), color, Px( 1.6f ) );
            }
            break;

            case Icon::Timeline:
            {
                // Gantt chart: three staggered bars
                float const lefts[] = { -8.0f, -3.0f, -6.0f };
                float const rights[] = { 1.0f, 8.0f, 4.0f };
                for( int i = 0; i < 3; i++ )
                {
                    float const y = c.y + Px( static_cast<float>( i - 1 ) * 5.5f );
                    pDrawList->AddRectFilled( ImVec2( c.x + Px( lefts[i] ), y - Px( 1.75f ) ), ImVec2( c.x + Px( rights[i] ), y + Px( 1.75f ) ), color, Px( 1.75f ) );
                }
            }
            break;

            case Icon::Reports:
            {
                // Three bars on a baseline
                float const base = c.y + Px( 8 );
                float const heights[] = { 7.0f, 15.0f, 11.0f };
                for( int i = 0; i < 3; i++ )
                {
                    float const x = c.x + Px( static_cast<float>( i - 1 ) * 5.5f );
                    pDrawList->AddRectFilled( ImVec2( x - Px( 1.75f ), base - Px( heights[i] ) ), ImVec2( x + Px( 1.75f ), base ), color, Px( 1 ) );
                }
            }
            break;

            case Icon::Programs:
            {
                // 2x2 app grid
                float const cell = Px( 6.5f );
                float const gap = Px( 2.5f );
                for( int i = 0; i < 4; i++ )
                {
                    float const x = c.x + ( i % 2 == 0 ? -cell - gap / 2 : gap / 2 );
                    float const y = c.y + ( i < 2 ? -cell - gap / 2 : gap / 2 );
                    pDrawList->AddRect( ImVec2( x, y ), ImVec2( x + cell, y + cell ), color, Px( 2 ), 0, Px( 1.6f ) );
                }
            }
            break;

            case Icon::Settings:
            {
                // Gear: 8 teeth around a ring
                for( int i = 0; i < 8; i++ )
                {
                    float const angle = static_cast<float>( i ) * 0.785398f;  // 45 degrees
                    ImVec2 const dir( std::cos( angle ), std::sin( angle ) );
                    pDrawList->AddLine( ImVec2( c.x + dir.x * Px( 5 ), c.y + dir.y * Px( 5 ) ), ImVec2( c.x + dir.x * Px( 8.5f ), c.y + dir.y * Px( 8.5f ) ), color, Px( 3 ) );
                }
                pDrawList->AddCircle( c, Px( 5.5f ), color, 0, Px( 2.2f ) );
            }
            break;

            case Icon::Pin:
            {
                // Push pin: head, collar and needle
                pDrawList->AddRectFilled( ImVec2( c.x - Px( 3.5f ), c.y - Px( 8 ) ), ImVec2( c.x + Px( 3.5f ), c.y - Px( 1 ) ), color, Px( 1.5f ) );
                pDrawList->AddLine( ImVec2( c.x - Px( 6.5f ), c.y - Px( 0.5f ) ), ImVec2( c.x + Px( 6.5f ), c.y - Px( 0.5f ) ), color, Px( 2 ) );
                pDrawList->AddLine( ImVec2( c.x, c.y ), ImVec2( c.x, c.y + Px( 8 ) ), color, Px( 1.6f ) );
            }
            break;

            case Icon::Expand:
            {
                // Two arrows pointing out to opposite corners
                float const r = Px( 7 );
                float const head = Px( 4.5f );
                for( float const s : { 1.0f, -1.0f } )
                {
                    ImVec2 const tip( c.x + s * r, c.y - s * r );
                    pDrawList->AddLine( ImVec2( c.x + s * Px( 1.5f ), c.y - s * Px( 1.5f ) ), tip, color, Px( 1.6f ) );
                    pDrawList->AddLine( tip, ImVec2( tip.x - s * head, tip.y ), color, Px( 1.6f ) );
                    pDrawList->AddLine( tip, ImVec2( tip.x, tip.y + s * head ), color, Px( 1.6f ) );
                }
            }
            break;
        }
    }

    bool IconButton( char const* pId, Icon icon, char const* pTooltip, bool active )
    {
        ImVec2 const pos = ImGui::GetCursorScreenPos();
        float const size = Px( 28 );
        bool const clicked = ImGui::InvisibleButton( pId, ImVec2( size, size ) );
        bool const hovered = ImGui::IsItemHovered();
        ImVec2 const c( pos.x + size / 2, pos.y + size / 2 );

        ImU32 color = active ? g_accent : Fade( g_text, 0.8f );
        if( hovered )
        {
            color = ( icon == Icon::Delete ) ? g_red : g_accent;
        }

        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        if( hovered )
        {
            pDrawList->AddCircleFilled( c, Px( 13 ), g_line );
        }

        DrawIcon( pDrawList, icon, c, color );
        ImGui::SetItemTooltip( "%s", pTooltip );
        return clicked;
    }

    bool RailItem( char const* pId, Icon icon, char const* pTooltip, bool active )
    {
        ImVec2 const pos = ImGui::GetCursorScreenPos();
        float const size = Px( 40 );
        bool const clicked = ImGui::InvisibleButton( pId, ImVec2( size, size ) );
        bool const hovered = ImGui::IsItemHovered();
        ImDrawList* pDrawList = ImGui::GetWindowDrawList();
        if( active || hovered )
        {
            pDrawList->AddRectFilled( pos, ImVec2( pos.x + size, pos.y + size ), active ? g_accentDark : g_card, Px( 10 ) );
        }

        DrawIcon( pDrawList, icon, ImVec2( pos.x + size / 2, pos.y + size / 2 ), active ? g_accent : ( hovered ? g_text : g_muted ) );
        ImGui::SetItemTooltip( "%s", pTooltip );
        return clicked;
    }

    std::optional<std::string> Chips( char const* pId, std::vector<std::string> const& names, char const* pEmptyText )
    {
        if( names.empty() )
        {
            Label( pEmptyText, g_bodySize, g_muted );
            return std::nullopt;
        }

        std::optional<std::string> clicked;
        float const rightEdge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        float const spacing = ImGui::GetStyle().ItemSpacing.x;
        ImGui::PushID( pId );
        ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, Px( 14 ) );
        for( size_t i = 0; i < names.size(); i++ )
        {
            std::string const label = "+  " + names[i];
            if( i > 0 )
            {
                SameLineIfFits( TextWidth( label ) + ImGui::GetStyle().FramePadding.x * 2, rightEdge, spacing );
            }

            ImGui::PushID( static_cast<int>( i ) );
            if( ImGui::Button( label.c_str(), ImVec2( 0, Px( 30 ) ) ) )
            {
                clicked = names[i];
            }
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        ImGui::PopID();
        return clicked;
    }

    void SameLineIfFits( float width, float rightEdge, float spacing )
    {
        if( ImGui::GetItemRectMax().x + spacing + width <= rightEdge )
        {
            ImGui::SameLine( 0, spacing );
        }
    }
}
