#include "Core/Store.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdlib>
#include <format>
#include <fstream>
#include <sstream>

#include "Core/Text.h"
#include "Core/TimeUtil.h"

//-------------------------------------------------------------------------

namespace Kui::Core
{
    static std::string ReadFile( std::filesystem::path const& path )
    {
        std::ifstream file( path, std::ios::binary );
        std::ostringstream ss;
        ss << file.rdbuf();
        return ss.str();
    }

    static bool WriteFile( std::filesystem::path const& path, std::string const& body )
    {
        std::ofstream file( path, std::ios::binary | std::ios::trunc );
        file << body;
        return static_cast<bool>( file );
    }

    // Write to .tmp then rename, so a reader never sees half a file
    static void WriteAtomic( std::filesystem::path const& path, std::string const& body )
    {
        std::filesystem::path tmp = path;
        tmp += ".tmp";
        if( WriteFile( tmp, body ) )
        {
            std::error_code ec;
            std::filesystem::rename( tmp, path, ec );
        }
    }

    static std::optional<nlohmann::ordered_json> ReadJson( std::filesystem::path const& path )
    {
        nlohmann::ordered_json j = nlohmann::ordered_json::parse( ReadFile( path ), nullptr, false );
        if( j.is_discarded() )
        {
            return std::nullopt;
        }
        return j;
    }

    // Missing or wrong-typed fields keep their default (serde(default) in the original)
    template<typename T>
    static void Get( nlohmann::ordered_json const& j, char const* pKey, T& out )
    {
        auto it = j.find( pKey );
        if( it == j.end() || it->is_null() )
        {
            return;
        }

        try
        {
            out = it->get<T>();
        }
        catch( nlohmann::ordered_json::exception const& )
        {}
    }

    static std::string EntryLine( Entry const& e )
    {
        nlohmann::ordered_json j;
        j["start"] = e.m_start;
        j["end"] = e.m_end;
        j["description"] = e.m_description;
        j["project"] = e.m_project;
        j["auto"] = e.m_isAuto;
        return j.dump();
    }

    static bool SameEntry( Entry const& a, Entry const& b )
    {
        return a.m_start == b.m_start && a.m_end == b.m_end && a.m_description == b.m_description;
    }

    // Rules
    //-------------------------------------------------------------------------

    bool Rule::Matches( std::string const& textLower ) const
    {
        std::string const keyword = ToLower( Trim( m_keyword ) );
        if( !m_enabled || keyword.empty() )
        {
            return false;
        }

        if( m_mode == Mode::Phrase )
        {
            return Contains( textLower, keyword );
        }

        for( std::string const& word : SplitWhitespace( keyword ) )
        {
            if( !Contains( textLower, word ) )
            {
                return false;
            }
        }
        return true;
    }

    std::string Rule::Label() const
    {
        std::string const description = Trim( m_description );
        return description.empty() ? Trim( m_keyword ) : description;
    }

    Rule const* FindRule( std::vector<Rule> const& rules, std::string const& app, std::string const& title )
    {
        std::string const text = ToLower( app + " " + title );
        for( Rule const& rule : rules )
        {
            if( rule.Matches( text ) )
            {
                return &rule;
            }
        }
        return nullptr;
    }

    // Store
    //-------------------------------------------------------------------------

    std::filesystem::path Store::DefaultDir()
    {
        wchar_t* pAppData = nullptr;
        size_t length = 0;
        _wdupenv_s( &pAppData, &length, L"APPDATA" );
        std::filesystem::path const dir = std::filesystem::path( pAppData ? pAppData : L"." ) / L"Kui";
        std::free( pAppData );
        return dir;
    }

    bool Store::BumpRecent( std::vector<std::string>& list, std::string const& app )
    {
        if( app.empty() || ( !list.empty() && list.front() == app ) )
        {
            return false;
        }

        std::erase( list, app );
        list.insert( list.begin(), app );
        if( list.size() > s_recentMax )
        {
            list.resize( s_recentMax );
        }
        return true;
    }

    Store::Store( std::filesystem::path dir )
        : m_dir( std::move( dir ) )
    {
        std::error_code ec;
        std::filesystem::create_directories( m_dir, ec );
    }

    std::filesystem::path Store::File( std::string const& name ) const
    {
        return m_dir / name;
    }

    std::optional<std::filesystem::file_time_type> Store::Modified( char const* pName ) const
    {
        std::error_code ec;
        auto const t = std::filesystem::last_write_time( File( pName ), ec );
        if( ec )
        {
            return std::nullopt;
        }
        return t;
    }

    // Settings
    //-------------------------------------------------------------------------

    Settings Store::LoadSettings() const
    {
        Settings s;
        auto const j = ReadJson( File( "settings.json" ) );
        if( !j || !j->is_object() )
        {
            return s;
        }

        auto const rules = j->find( "rules" );
        if( rules != j->end() && rules->is_array() )
        {
            for( auto const& rj : *rules )
            {
                Rule r;
                std::string mode;
                Get( rj, "id", r.m_id );
                Get( rj, "keyword", r.m_keyword );
                Get( rj, "mode", mode );
                Get( rj, "description", r.m_description );
                Get( rj, "project", r.m_project );
                Get( rj, "enabled", r.m_enabled );
                r.m_mode = ( mode == "AllWords" ) ? Mode::AllWords : Mode::Phrase;
                s.m_rules.push_back( r );
            }
        }

        Get( *j, "tracking", s.m_tracking );
        Get( *j, "stop_on_no_match", s.m_stopOnNoMatch );
        Get( *j, "focus_on", s.m_focusOn );
        Get( *j, "focus_secs", s.m_focusSecs );
        Get( *j, "idle_on", s.m_idleOn );
        Get( *j, "idle_minutes", s.m_idleMinutes );
        Get( *j, "first_run_done", s.m_firstRunDone );
        Get( *j, "work_hours", s.m_workHours );
        return s;
    }

    void Store::SaveSettings( Settings const& s ) const
    {
        nlohmann::ordered_json rules = nlohmann::ordered_json::array();
        for( Rule const& r : s.m_rules )
        {
            nlohmann::ordered_json rj;
            rj["id"] = r.m_id;
            rj["keyword"] = r.m_keyword;
            rj["mode"] = ( r.m_mode == Mode::AllWords ) ? "AllWords" : "Phrase";
            rj["description"] = r.m_description;
            rj["project"] = r.m_project;
            rj["enabled"] = r.m_enabled;
            rules.push_back( rj );
        }

        nlohmann::ordered_json j;
        j["rules"] = rules;
        j["tracking"] = s.m_tracking;
        j["stop_on_no_match"] = s.m_stopOnNoMatch;
        j["focus_on"] = s.m_focusOn;
        j["focus_secs"] = s.m_focusSecs;
        j["idle_on"] = s.m_idleOn;
        j["idle_minutes"] = s.m_idleMinutes;
        j["first_run_done"] = s.m_firstRunDone;
        j["work_hours"] = s.m_workHours;
        WriteAtomic( File( "settings.json" ), j.dump( 2 ) );
    }

    // Timer + entries
    //-------------------------------------------------------------------------

    std::optional<Timer> Store::LoadTimer() const
    {
        auto const j = ReadJson( File( "timer.json" ) );
        if( !j || !j->is_object() )
        {
            return std::nullopt;
        }

        Timer t;
        Get( *j, "start", t.m_start );
        Get( *j, "description", t.m_description );
        Get( *j, "project", t.m_project );
        auto const rule = j->find( "rule" );
        if( rule != j->end() && rule->is_number_unsigned() )
        {
            t.m_rule = rule->get<uint64_t>();
        }
        return t;
    }

    void Store::SaveTimer( Timer const& t ) const
    {
        nlohmann::ordered_json j;
        j["start"] = t.m_start;
        j["description"] = t.m_description;
        j["project"] = t.m_project;
        j["rule"] = t.m_rule ? nlohmann::ordered_json( *t.m_rule ) : nlohmann::ordered_json( nullptr );
        WriteAtomic( File( "timer.json" ), j.dump( 2 ) );
    }

    // ponytail: window and tracker both touch timer.json without a lock; a same-second race can drop
    // or double one entry. Move to a single writer (tracker over a pipe) if that ever shows up.
    void Store::StopTimer( int64_t end ) const
    {
        std::optional<Timer> const t = LoadTimer();
        if( !t )
        {
            return;
        }

        std::error_code ec;
        std::filesystem::remove( File( "timer.json" ), ec );
        if( end > t->m_start )
        {
            AppendEntry( { t->m_start, end, t->m_description, t->m_project, t->m_rule.has_value() } );
        }
    }

    void Store::StartTimer( Timer const& t ) const
    {
        StopTimer( t.m_start );
        SaveTimer( t );
    }

    void Store::AppendEntry( Entry const& e ) const
    {
        std::ofstream file( File( "entries.jsonl" ), std::ios::binary | std::ios::app );
        file << EntryLine( e ) << "\n";
    }

    std::vector<Entry> Store::LoadEntries() const
    {
        std::vector<Entry> out;
        std::istringstream in( ReadFile( File( "entries.jsonl" ) ) );
        for( std::string line; std::getline( in, line ); )
        {
            nlohmann::ordered_json const j = nlohmann::ordered_json::parse( line, nullptr, false );
            if( j.is_discarded() || !j.is_object() || !j.contains( "start" ) || !j.contains( "end" ) )
            {
                continue;
            }

            Entry e;
            Get( j, "start", e.m_start );
            Get( j, "end", e.m_end );
            Get( j, "description", e.m_description );
            Get( j, "project", e.m_project );
            Get( j, "auto", e.m_isAuto );
            out.push_back( std::move( e ) );
        }
        return out;
    }

    void Store::RewriteEntries( std::vector<Entry> const& del, std::vector<Entry> const& edit, std::string const& description, std::string const& project ) const
    {
        std::string body;
        for( Entry& e : LoadEntries() )
        {
            if( std::any_of( del.begin(), del.end(), [&]( Entry const& d ) { return SameEntry( d, e ); } ) )
            {
                continue;
            }

            if( std::any_of( edit.begin(), edit.end(), [&]( Entry const& d ) { return SameEntry( d, e ); } ) )
            {
                e.m_description = description;
                e.m_project = project;
            }
            body += EntryLine( e ) + "\n";
        }
        WriteAtomic( File( "entries.jsonl" ), body );
    }

    std::optional<std::filesystem::path> Store::ExportCsv() const
    {
        auto quote = []( std::string const& s ) { return "\"" + ReplaceAll( s, "\"", "\"\"" ) + "\""; };

        std::vector<Entry> entries = LoadEntries();
        std::stable_sort( entries.begin(), entries.end(), []( Entry const& a, Entry const& b ) { return a.m_start < b.m_start; } );

        std::string out = "Start,End,Duration (h),Description,Project,Auto\n";
        for( Entry const& e : entries )
        {
            double const hours = static_cast<double>( e.m_end - e.m_start ) / 3600.0;
            out += std::format( "{},{},{:.3f},{},{},{}\n", DateTime( e.m_start ), DateTime( e.m_end ), hours, quote( e.m_description ), quote( e.m_project ), e.m_isAuto ? "true" : "false" );
        }

        std::filesystem::path const path = File( "time-entries.csv" );
        if( !WriteFile( path, out ) )
        {
            return std::nullopt;
        }
        return path;
    }

    // Tracker status + recent programs
    //-------------------------------------------------------------------------

    std::optional<Status> Store::LoadStatus() const
    {
        auto const j = ReadJson( File( "tracker.json" ) );
        if( !j || !j->is_object() )
        {
            return std::nullopt;
        }

        Status s;
        Get( *j, "heartbeat", s.m_heartbeat );
        Get( *j, "app", s.m_app );
        Get( *j, "title", s.m_title );
        return s;
    }

    void Store::SaveStatus( Status const& s ) const
    {
        nlohmann::ordered_json j;
        j["heartbeat"] = s.m_heartbeat;
        j["app"] = s.m_app;
        j["title"] = s.m_title;
        WriteAtomic( File( "tracker.json" ), j.dump( 2 ) );
    }

    std::vector<std::string> Store::LoadRecent() const
    {
        std::vector<std::string> recent;
        auto const j = ReadJson( File( "recent.json" ) );
        if( !j || !j->is_array() )
        {
            return recent;
        }

        for( auto const& item : *j )
        {
            if( item.is_string() )
            {
                recent.push_back( item.get<std::string>() );
            }
        }
        return recent;
    }

    void Store::SaveRecent( std::vector<std::string> const& recent ) const
    {
        WriteAtomic( File( "recent.json" ), nlohmann::ordered_json( recent ).dump( 2 ) );
    }

    // Window placement
    //-------------------------------------------------------------------------

    std::optional<Placement> Store::LoadPlacement() const
    {
        auto const j = ReadJson( File( "window.json" ) );
        if( !j || !j->is_object() )
        {
            return std::nullopt;
        }

        Placement p;
        Get( *j, "x", p.m_x );
        Get( *j, "y", p.m_y );
        Get( *j, "width", p.m_width );
        Get( *j, "height", p.m_height );
        Get( *j, "maximized", p.m_isMaximized );
        if( p.m_width <= 0 || p.m_height <= 0 )
        {
            return std::nullopt;
        }
        return p;
    }

    void Store::SavePlacement( Placement const& p ) const
    {
        nlohmann::ordered_json j;
        j["x"] = p.m_x;
        j["y"] = p.m_y;
        j["width"] = p.m_width;
        j["height"] = p.m_height;
        j["maximized"] = p.m_isMaximized;
        WriteAtomic( File( "window.json" ), j.dump( 2 ) );
    }
}
