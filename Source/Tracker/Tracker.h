#pragma once

#include <cstdint>
#include <optional>

#include "Core/Store.h"

//-------------------------------------------------------------------------
// The background tracker: a tray icon and no window. Wakes once a second, looks at the program in front
// and starts/stops the timer from the rules. Started at sign-in from the Run registry key (`kui.exe --tracker`).
//-------------------------------------------------------------------------

namespace Kui::Tracker
{
    // The tracker writes its status this often
    constexpr int64_t g_heartbeatSecs = 10;

    //-------------------------------------------------------------------------

    // Has the tracker written a heartbeat recently?
    bool IsAlive( std::optional<Core::Status> const& status );

    // Runs the tracker until "Exit tracker", sign-out or WM_CLOSE. Returns at once if one is already running.
    int Run( Core::Store const& store );
}
