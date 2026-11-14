#pragma once

#include <string>
#include <string_view>
#include <vector>

//-------------------------------------------------------------------------
// First-run setup: finds installed creative apps, then time-wasters (sites + Steam games) to offer as rules.
//-------------------------------------------------------------------------

namespace Kui::Setup
{
    // Project that sites and games picked in setup are logged under
    constexpr char g_wasteProject[] = "Wasted time";

    //-------------------------------------------------------------------------

    // One checkbox in the setup lists
    struct Pick
    {
        std::string     m_label;
        std::string     m_keyword;
        bool            m_on = true;
    };

    struct Detected
    {
        std::vector<Pick>   m_apps;
        std::vector<Pick>   m_sites;
        std::vector<Pick>   m_games;
    };

    //-------------------------------------------------------------------------

    // Scans the uninstall registry keys and Steam libraries
    Detected Detect();

    // Every `"key" "value"` pair's value in a Steam .vdf/.acf file
    std::vector<std::string> VdfValues( std::string_view text, std::string_view key );
}
