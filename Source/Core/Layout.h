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
    // Entries overlapping [from, to). `entries` is newest-first and `longest` is the longest entry's secs, so this is two binary searches, not a full scan.
    std::vector<Entry const*> Span( std::vector<Entry> const& entries, int64_t from, int64_t to, int64_t longest );

    // Greedy lane packing. `spans` sorted by start; returns (lane, lanes in its overlap cluster) per span.
    std::vector<std::pair<size_t, size_t>> Lanes( std::vector<std::pair<int64_t, int64_t>> const& spans );

    // Runs of consecutive non-zero values, as (first, last) index pairs. Timeline bars are runs of tracked days.
    std::vector<std::pair<size_t, size_t>> Runs( std::vector<int64_t> const& values );
}
