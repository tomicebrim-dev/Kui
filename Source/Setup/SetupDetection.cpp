#include "Setup/SetupDetection.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "Core/Text.h"
#include "Platform/Platform.h"

//-------------------------------------------------------------------------

namespace Kui::Setup
{
    struct KnownApp
    {
        char const*     m_pNeedle;      // Text in the installer's DisplayName
        char const*     m_pKeyword;     // Rule keyword: exe name or window-title word
        char const*     m_pLabel;
    };

    // ponytail: only apps with a normal installer (Uninstall registry key); Store/portable installs aren't found. Add App Paths / Store scan if missed.
    constexpr KnownApp g_knownApps[] =
    {
        { "blender", "blender", "Blender" },
        { "clip studio", "clipstudiopaint", "Clip Studio Paint" },
        { "adobe photoshop", "photoshop", "Photoshop" },
        { "adobe illustrator", "illustrator", "Illustrator" },
        { "adobe after effects", "afterfx", "After Effects" },
        { "adobe premiere pro", "premiere pro", "Premiere Pro" },
        { "adobe indesign", "indesign", "InDesign" },
        { "adobe lightroom", "lightroom", "Lightroom" },
        { "adobe audition", "adobe audition", "Audition" },
        { "adobe acrobat", "acrobat", "Acrobat" },
        { "substance 3d painter", "substance 3d painter", "Substance 3D Painter" },
        { "krita", "krita", "Krita" },
        { "gimp", "gimp", "GIMP" },
        { "inkscape", "inkscape", "Inkscape" },
        { "aseprite", "aseprite", "Aseprite" },
        { "affinity photo", "affinity photo", "Affinity Photo" },
        { "affinity designer", "affinity designer", "Affinity Designer" },
        { "figma", "figma", "Figma" },
        { "autodesk maya", "maya", "Maya" },
        { "zbrush", "zbrush", "ZBrush" },
        { "cinema 4d", "cinema 4d", "Cinema 4D" },
        { "houdini", "houdini", "Houdini" },
        { "davinci resolve", "resolve", "DaVinci Resolve" },
        { "kdenlive", "kdenlive", "Kdenlive" },
        { "obs studio", "obs64", "OBS Studio" },
        { "audacity", "audacity", "Audacity" },
        { "fl studio", "fl studio", "FL Studio" },
        { "ableton live", "ableton live", "Ableton Live" },
        { "reaper", "reaper", "REAPER" },
        { "microsoft visual studio code", "visual studio code", "VS Code" },
    };

    // Matched against browser window titles: (keyword, label)
    constexpr char const* g_sites[][2] =
    {
        { "youtube", "YouTube" },
        { "reddit", "Reddit" },
        { "twitch", "Twitch" },
        { "netflix", "Netflix" },
        { "facebook", "Facebook" },
        { "instagram", "Instagram" },
        { "tiktok", "TikTok" },
        { "discord", "Discord" },
    };

    static std::string ReadText( std::filesystem::path const& path )
    {
        std::ifstream file( path, std::ios::binary );
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    // DisplayName of every installed program (64-bit, 32-bit and per-user uninstall lists)
    static std::vector<std::string> InstalledPrograms()
    {
        struct UninstallKey
        {
            HKEY            m_root;
            char const*     m_pPath;
        };

        static UninstallKey const keys[] =
        {
            { HKEY_LOCAL_MACHINE, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall" },
            { HKEY_LOCAL_MACHINE, "Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall" },
            { HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall" },
        };

        std::vector<std::string> names;
        for( UninstallKey const& key : keys )
        {
            for( std::string const& subkey : Platform::RegSubkeys( key.m_root, key.m_pPath ) )
            {
                std::optional<std::string> name = Platform::RegString( key.m_root, std::string( key.m_pPath ) + "\\" + subkey, "DisplayName" );
                if( name )
                {
                    names.push_back( *name );
                }
            }
        }
        return names;
    }

    // Installed Steam games' names, from every library's appmanifest_*.acf
    static std::vector<std::string> SteamGames()
    {
        std::optional<std::string> steamPath = Platform::RegString( HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath" );
        if( !steamPath )
        {
            return {};
        }

        std::filesystem::path const steam = Core::Widen( *steamPath );
        std::vector<std::filesystem::path> libraries = { steam };
        for( std::string const& path : VdfValues( ReadText( steam / "steamapps" / "libraryfolders.vdf" ), "path" ) )
        {
            libraries.push_back( Core::Widen( path ) );
        }

        std::vector<std::string> games;
        for( std::filesystem::path const& library : libraries )
        {
            std::error_code ec;
            for( auto const& file : std::filesystem::directory_iterator( library / "steamapps", ec ) )
            {
                std::string const fileName = Core::ToLower( Core::Narrow( file.path().filename().wstring() ) );
                if( !fileName.starts_with( "appmanifest_" ) || !fileName.ends_with( ".acf" ) )
                {
                    continue;
                }

                std::vector<std::string> names = VdfValues( ReadText( file.path() ), "name" );
                if( names.empty() )
                {
                    continue;
                }

                std::string const& name = names.front();
                bool const isTool = Core::Contains( name, "Redistributable" ) || name.starts_with( "Proton" ) || name.starts_with( "Steam Linux Runtime" );
                if( !isTool && std::find( games.begin(), games.end(), name ) == games.end() )
                {
                    games.push_back( name );
                }
            }
        }

        std::sort( games.begin(), games.end(), []( std::string const& a, std::string const& b ) { return Core::ToLower( a ) < Core::ToLower( b ); } );
        return games;
    }

    //-------------------------------------------------------------------------

    std::vector<std::string> VdfValues( std::string_view text, std::string_view key )
    {
        std::vector<std::string> values;
        std::istringstream in { std::string( text ) };
        for( std::string line; std::getline( in, line ); )
        {
            std::vector<std::string> parts;
            std::istringstream fields( Core::Trim( line ) );
            for( std::string part; std::getline( fields, part, '"' ); )
            {
                parts.push_back( part );
            }

            if( parts.size() >= 4 && Core::IEqualsAscii( parts[1], key ) )
            {
                values.push_back( Core::ReplaceAll( parts[3], "\\\\", "\\" ) );
            }
        }
        return values;
    }

    Detected Detect()
    {
        std::vector<std::string> const steam = SteamGames();

        std::vector<std::string> names = InstalledPrograms();
        names.insert( names.end(), steam.begin(), steam.end() );
        for( std::string& name : names )
        {
            name = Core::ToLower( name );
        }

        Detected detected;
        for( KnownApp const& app : g_knownApps )
        {
            bool const isInstalled = std::any_of( names.begin(), names.end(), [&]( std::string const& n ) { return Core::Contains( n, app.m_pNeedle ); } );
            bool const isListed = std::any_of( detected.m_apps.begin(), detected.m_apps.end(), [&]( Pick const& p ) { return p.m_keyword == app.m_pKeyword; } );
            if( isInstalled && !isListed )
            {
                detected.m_apps.push_back( { app.m_pLabel, app.m_pKeyword, true } );
            }
        }

        for( auto const& site : g_sites )
        {
            detected.m_sites.push_back( { site[1], site[0], false } );
        }

        // Steam also ships apps (Blender, Krita, Clip Studio...): those count as apps, not games
        for( std::string const& game : steam )
        {
            std::string const lower = Core::ToLower( game );
            bool const isApp = std::any_of( std::begin( g_knownApps ), std::end( g_knownApps ), [&]( KnownApp const& a ) { return Core::Contains( lower, a.m_pNeedle ); } );
            if( isApp )
            {
                continue;
            }

            std::string keyword = game;
            for( char const* pMark : { "™", "®", "©" } )
            {
                keyword = Core::ReplaceAll( keyword, pMark, "" );
            }
            detected.m_games.push_back( { game, Core::Trim( keyword ), true } );
        }
        return detected;
    }
}
