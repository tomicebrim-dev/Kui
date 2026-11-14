#include "Core/TimeUtil.h"

#include <chrono>
#include <ctime>
#include <format>

//-------------------------------------------------------------------------

namespace Kui::Core
{
    static std::chrono::sys_days SysDays( Day d )
    {
        return std::chrono::sys_days { std::chrono::days { d } };
    }

    static std::tm LocalTm( int64_t t )
    {
        std::tm tm {};
        time_t const tt = static_cast<time_t>( t );
        localtime_s( &tm, &tt );
        return tm;
    }

    //-------------------------------------------------------------------------

    int64_t Now()
    {
        auto const sinceEpoch = std::chrono::system_clock::now().time_since_epoch();
        return std::chrono::duration_cast<std::chrono::seconds>( sinceEpoch ).count();
    }

    Day DayOf( int64_t t )
    {
        std::tm const tm = LocalTm( t );
        std::chrono::year const y { tm.tm_year + 1900 };
        std::chrono::month const m { static_cast<unsigned>( tm.tm_mon + 1 ) };
        std::chrono::day const d { static_cast<unsigned>( tm.tm_mday ) };
        return static_cast<Day>( std::chrono::sys_days { y / m / d }.time_since_epoch().count() );
    }

    int64_t DayStart( Day d )
    {
        std::chrono::year_month_day const ymd { SysDays( d ) };
        std::tm tm {};
        tm.tm_year = static_cast<int>( ymd.year() ) - 1900;
        tm.tm_mon = static_cast<int>( static_cast<unsigned>( ymd.month() ) ) - 1;
        tm.tm_mday = static_cast<int>( static_cast<unsigned>( ymd.day() ) );
        tm.tm_isdst = -1;
        return static_cast<int64_t>( mktime( &tm ) );
    }

    int WeekdayIso( Day d )
    {
        return static_cast<int>( std::chrono::weekday { SysDays( d ) }.iso_encoding() );
    }

    int DayOfMonth( Day d )
    {
        return static_cast<int>( static_cast<unsigned>( std::chrono::year_month_day { SysDays( d ) }.day() ) );
    }

    int IsoWeek( Day d )
    {
        return std::stoi( std::format( "{:%V}", SysDays( d ) ) );
    }

    Day WeekMonday( Day today, int weekOffset )
    {
        return today - ( WeekdayIso( today ) - 1 ) + 7 * weekOffset;
    }

    std::string FormatDay( Day d, char const* pChronoFormat )
    {
        std::chrono::sys_days const sd = SysDays( d );
        return std::vformat( pChronoFormat, std::make_format_args( sd ) );
    }

    std::string DayMonth( Day d, bool longMonth )
    {
        return std::to_string( DayOfMonth( d ) ) + " " + FormatDay( d, longMonth ? "{:%B}" : "{:%b}" );
    }

    std::string DayName( Day d, Day today )
    {
        int const diff = today - d;
        if( diff == 0 )
        {
            return "Today";
        }

        if( diff == 1 )
        {
            return "Yesterday";
        }

        if( diff >= 2 && diff <= 6 )
        {
            return FormatDay( d, "{:%A}" );
        }

        return FormatDay( d, "{:%a}" ) + " " + DayMonth( d );
    }

    std::string FormatHMS( int64_t secs )
    {
        int64_t const s = secs < 0 ? 0 : secs;
        return std::format( "{}:{:02}:{:02}", s / 3600, s / 60 % 60, s % 60 );
    }

    std::string FormatHM( int64_t secs )
    {
        int64_t const m = ( secs < 0 ? 0 : secs ) / 60;
        return m >= 60 ? std::format( "{}h {:02}m", m / 60, m % 60 ) : std::format( "{}m", m );
    }

    std::string ClockHM( int64_t t )
    {
        std::tm const tm = LocalTm( t );
        return std::format( "{:02}:{:02}", tm.tm_hour, tm.tm_min );
    }

    std::string DateTime( int64_t t )
    {
        std::tm const tm = LocalTm( t );
        char buffer[32];
        strftime( buffer, sizeof( buffer ), "%Y-%m-%d %H:%M:%S", &tm );
        return buffer;
    }
}
