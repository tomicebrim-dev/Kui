#include "Core/Text.h"

#include <windows.h>

#include <sstream>

//-------------------------------------------------------------------------

namespace Kui::Core
{
    std::wstring Widen( std::string_view s )
    {
        if( s.empty() )
        {
            return {};
        }

        int const size = static_cast<int>( s.size() );
        int const n = MultiByteToWideChar( CP_UTF8, 0, s.data(), size, nullptr, 0 );
        std::wstring w( n, L'\0' );
        MultiByteToWideChar( CP_UTF8, 0, s.data(), size, w.data(), n );
        return w;
    }

    std::string Narrow( std::wstring_view w )
    {
        if( w.empty() )
        {
            return {};
        }

        int const size = static_cast<int>( w.size() );
        int const n = WideCharToMultiByte( CP_UTF8, 0, w.data(), size, nullptr, 0, nullptr, nullptr );
        std::string s( n, '\0' );
        WideCharToMultiByte( CP_UTF8, 0, w.data(), size, s.data(), n, nullptr, nullptr );
        return s;
    }

    std::string ToLower( std::string_view s )
    {
        std::wstring w = Widen( s );
        if( !w.empty() )
        {
            CharLowerBuffW( w.data(), static_cast<DWORD>( w.size() ) );
        }
        return Narrow( w );
    }

    std::string Trim( std::string_view s )
    {
        char const* pWhitespace = " \t\r\n\v\f";
        size_t const a = s.find_first_not_of( pWhitespace );
        if( a == std::string_view::npos )
        {
            return {};
        }

        size_t const b = s.find_last_not_of( pWhitespace );
        return std::string( s.substr( a, b - a + 1 ) );
    }

    bool IEqualsAscii( std::string_view a, std::string_view b )
    {
        auto lower = []( char c ) { return ( c >= 'A' && c <= 'Z' ) ? static_cast<char>( c + 32 ) : c; };

        if( a.size() != b.size() )
        {
            return false;
        }

        for( size_t i = 0; i < a.size(); i++ )
        {
            if( lower( a[i] ) != lower( b[i] ) )
            {
                return false;
            }
        }
        return true;
    }

    bool Contains( std::string_view haystack, std::string_view needle )
    {
        return haystack.find( needle ) != std::string_view::npos;
    }

    std::vector<std::string> SplitWhitespace( std::string_view s )
    {
        std::vector<std::string> out;
        std::istringstream in { std::string( s ) };
        for( std::string word; in >> word; )
        {
            out.push_back( word );
        }
        return out;
    }

    std::string ReplaceAll( std::string s, std::string_view from, std::string_view to )
    {
        for( size_t i = 0; ( i = s.find( from, i ) ) != std::string::npos; i += to.size() )
        {
            s.replace( i, from.size(), to );
        }
        return s;
    }
}
