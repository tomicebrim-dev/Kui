#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "Core/Store.h"

//-------------------------------------------------------------------------
// Pure helpers behind the calendar and reports, kept out of the UI so they can be tested.
//-------------------------------------------------------------------------

namespace Kui::Core
{
    // Entries overlapping [from, to). `entries` is newest-first, so this is two binary searches, not a full scan.
    // ponytail: assumes no entry is longer than 24 h (true for app-switch tracking); widen the window if long manual entries matter.
    std::vector<Entry const*> Span( std::vector<Entry> const& entries, int64_t from, int64_t to );

    // Greedy lane packing. `spans` sorted by start; returns (lane, lanes in its overlap cluster) per span.
    std::vector<std::pair<size_t, size_t>> Lanes( std::vector<std::pair<int64_t, int64_t>> const& spans );
}
