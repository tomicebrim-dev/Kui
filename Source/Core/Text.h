#pragma once

#include <string>
#include <string_view>
#include <vector>

//-------------------------------------------------------------------------
// UTF-8 string helpers. All app strings are UTF-8, Win32 calls take UTF-16.
//-------------------------------------------------------------------------

namespace Kui::Core
{
    // UTF-8 to UTF-16, for Win32 calls
    std::wstring Widen( std::string_view s );

    // UTF-16 to UTF-8
    std::string Narrow( std::wstring_view s );

    // Unicode-aware lower case
    std::string ToLower( std::string_view s );

    // Strips leading and trailing whitespace
    std::string Trim( std::string_view s );

    // Case-insensitive compare, ASCII letters only
    bool IEqualsAscii( std::string_view a, std::string_view b );

    // Is the needle anywhere in the haystack?
    bool Contains( std::string_view haystack, std::string_view needle );

    // Splits on any run of whitespace
    std::vector<std::string> SplitWhitespace( std::string_view s );

    // Replaces every occurrence of `from` with `to`
    std::string ReplaceAll( std::string s, std::string_view from, std::string_view to );
}
