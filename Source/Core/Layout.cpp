#include "Core/Layout.h"

#include <algorithm>
#include <climits>

//-------------------------------------------------------------------------

namespace Kui::Core
{
    std::vector<Entry const*> Span( std::vector<Entry> const& entries, int64_t from, int64_t to )
    {
        auto a = std::partition_point( entries.begin(), entries.end(), [&]( Entry const& e ) { return e.m_start >= to; } );
        auto b = std::partition_point( entries.begin(), entries.end(), [&]( Entry const& e ) { return e.m_start >= from - 86400; } );
        if( b < a )
        {
            b = a;
        }

        std::vector<Entry const*> out;
        for( auto it = a; it != b; ++it )
        {
            if( it->m_end > from )
            {
                out.push_back( &*it );
            }
        }
        return out;
    }

    std::vector<std::pair<size_t, size_t>> Lanes( std::vector<std::pair<int64_t, int64_t>> const& spans )
    {
        std::vector<std::pair<size_t, size_t>> out( spans.size(), { 0, 1 } );
        std::vector<int64_t> laneEnds;
        size_t clusterFirst = 0;
        int64_t clusterEnd = INT64_MIN;

        // Every span in the finished cluster learns how many lanes it needed
        auto closeCluster = [&]( size_t upTo )
        {
            for( size_t k = clusterFirst; k < upTo; k++ )
            {
                out[k].second = std::max<size_t>( laneEnds.size(), 1 );
            }
        };

        for( size_t i = 0; i < spans.size(); i++ )
        {
            auto const [start, end] = spans[i];
            bool const isNewCluster = start >= clusterEnd;
            if( isNewCluster )
            {
                closeCluster( i );
                laneEnds.clear();
                clusterFirst = i;
            }

            auto const freeLane = std::find_if( laneEnds.begin(), laneEnds.end(), [&]( int64_t laneEnd ) { return laneEnd <= start; } );
            size_t const lane = static_cast<size_t>( freeLane - laneEnds.begin() );
            if( lane == laneEnds.size() )
            {
                laneEnds.push_back( end );
            }
            else
            {
                laneEnds[lane] = end;
            }

            out[i].first = lane;
            clusterEnd = isNewCluster ? end : std::max( clusterEnd, end );
        }

        closeCluster( spans.size() );
        return out;
    }
}
