#pragma once

#include <optional>
#include <string>
#include <string_view>

//-------------------------------------------------------------------------
// Self-update from GitHub releases: the latest release's .msi, installed per-user by msiexec.
//-------------------------------------------------------------------------

namespace Kui::Updater
{
    // A published release
    struct Release
    {
        std::string     m_version;      // "1.2.0", no leading "v"
        std::string     m_msiUrl;       // Empty when the release has no .msi
    };

    //-------------------------------------------------------------------------

    // This build's version, e.g. "1.0.0"
    char const* CurrentVersion();

    // Is version `a` newer than `b`? Compares up to four dotted numbers; a leading "v" and any "-suffix" are ignored.
    bool IsNewer( std::string_view a, std::string_view b );

    // Asks GitHub for the latest release. Blocks for up to ~10 s; nullopt when offline or the reply is unusable.
    std::optional<Release> FetchLatest();

    // Downloads the release's .msi to %TEMP%, runs it (progress bar only) and reopens Kui when it succeeds.
    // Quit right after a true return: the installer replaces kui.exe. False if the download failed.
    bool Install( Release const& release );
}
