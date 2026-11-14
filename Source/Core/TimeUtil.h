#pragma once

#include <cstdint>
#include <string>

//-------------------------------------------------------------------------
// Local-time helpers. Times are unix seconds.
//-------------------------------------------------------------------------

namespace Kui::Core
{
    // A local calendar date as days since 1970-01-01. Named so date math reads as dates, not counts.
    using Day = int32_t;

    // Current unix time in seconds
    int64_t Now();

    // Local calendar date of a unix time
    Day DayOf( int64_t t );

    // Unix time of local midnight at the start of a day
    int64_t DayStart( Day d );

    // 1 = Monday .. 7 = Sunday
    int WeekdayIso( Day d );

    // ISO-8601 week number
    int IsoWeek( Day d );

    // Monday of the week `weekOffset` weeks from the one holding `today`
    Day WeekMonday( Day today, int weekOffset );

    // 1 .. 31
    int DayOfMonth( Day d );

    // Formats a day with a std::format chrono spec, e.g. "{:%a}"
    std::string FormatDay( Day d, char const* pChronoFormat );

    // "3 Nov" or "3 November"
    std::string DayMonth( Day d, bool longMonth = false );

    // "Today", "Yesterday", "Monday" or "Mon 3 Nov"
    std::string DayName( Day d, Day today );

    // "1:02:03"
    std::string FormatHMS( int64_t secs );

    // "1h 02m" or "12m"
    std::string FormatHM( int64_t secs );

    // "14:05"
    std::string ClockHM( int64_t t );

    // "2026-11-14 14:05:09"
    std::string DateTime( int64_t t );
}
