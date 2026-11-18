#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

//-------------------------------------------------------------------------
// Data shared by the window and the background tracker: small JSON files in %APPDATA%\Kui.
// File formats match the original Rust app (TimeTrackLite), so its data folder can be copied over.
//-------------------------------------------------------------------------

namespace Kui::Core
{
    // How a rule keyword is matched against "<program> <window title>"
    enum class Mode
    {
        Phrase,     // The whole keyword appears as written
        AllWords,   // Every word appears, in any order
    };

    //-------------------------------------------------------------------------

    // Starts a timer whenever its keyword is in front
    struct Rule
    {
        public:

        // Lower-cased, trimmed keywords; the keyword text holds comma-separated alternatives
        std::vector<std::string> Keywords() const;

        // Does this rule match any keyword in the lower-cased "<program> <title>"? Disabled or blank rules never match.
        bool Matches( std::string const& textLower ) const;

        // The entry name: the description, or the keyword when that is blank
        std::string Label() const;

        //-------------------------------------------------------------------------

        public:

        uint64_t        m_id = 0;
        std::string     m_keyword;
        Mode            m_mode = Mode::Phrase;
        std::string     m_description;
        std::string     m_project;
        bool            m_enabled = true;
    };

    // First enabled rule whose keyword is in "<app> <title>" wins
    Rule const* FindRule( std::vector<Rule> const& rules, std::string const& app, std::string const& title );

    //-------------------------------------------------------------------------

    struct Settings
    {
        std::vector<Rule>   m_rules;
        bool                m_tracking = true;
        bool                m_stopOnNoMatch = true;
        bool                m_focusOn = false;
        int                 m_focusSecs = 5;
        bool                m_idleOn = true;
        int                 m_idleMinutes = 10;
        bool                m_firstRunDone = false;
        float               m_workHours = 8.0f;
        uint32_t            m_workDays = 0x1f;      // Bit 0 = Monday ... bit 6 = Sunday
        float               m_textScale = 1.2f;     // Multiplies every text size
        bool                m_checkUpdates = true;  // Look for a new release when the window opens
        bool                m_autostart = true;     // The user's choice; the installer rewrites the Run key on every update
        std::map<std::string, uint32_t> m_projectColors;    // Picked colors (ImU32) by project; others use the palette
        std::set<std::string>           m_nonWorkProjects;  // Projects left out of Workload (e.g. wasted time)
    };

    // The one running timer, started by hand or by a rule
    struct Timer
    {
        int64_t                     m_start = 0;
        std::string                 m_description;
        std::string                 m_project;
        std::optional<uint64_t>     m_rule;
    };

    // One finished stretch of time
    struct Entry
    {
        int64_t         m_start = 0;
        int64_t         m_end = 0;
        std::string     m_description;
        std::string     m_project;
        bool            m_isAuto = false;
    };

    // Written by the tracker every few seconds so the window knows it is alive and what is in front
    struct Status
    {
        int64_t         m_heartbeat = 0;
        std::string     m_app;
        std::string     m_title;
        bool            m_safeMode = false; // An anti-cheat is running, so the tracker is scanning lightly
    };

    // Main window position and size, restored on the next start
    struct Placement
    {
        int     m_x = 0;
        int     m_y = 0;
        int     m_width = 0;
        int     m_height = 0;
        bool    m_isMaximized = false;
    };

    //-------------------------------------------------------------------------

    // Reads and writes the files in one data folder
    class Store
    {
        public:

        // %APPDATA%\Kui
        static std::filesystem::path DefaultDir();

        // Moves `app` to the front of `list`, capped at s_recentMax. False if it was already first.
        static bool BumpRecent( std::vector<std::string>& list, std::string const& app );

        //-------------------------------------------------------------------------

        public:

        static constexpr size_t s_recentMax = 20;

        //-------------------------------------------------------------------------

        public:

        // Creates the folder if missing
        explicit Store( std::filesystem::path dir );

        inline std::filesystem::path const& GetDir() const { return m_dir; }

        // Last write time of a file in the folder, if it exists
        std::optional<std::filesystem::file_time_type> Modified( char const* pName ) const;

        Settings LoadSettings() const;
        void SaveSettings( Settings const& s ) const;

        std::optional<Timer> LoadTimer() const;
        void SaveTimer( Timer const& t ) const;

        // Removes the timer and logs it as an entry ending at `end`
        void StopTimer( int64_t end ) const;

        // Stops any running timer at t.m_start, then starts t
        void StartTimer( Timer const& t ) const;

        void AppendEntry( Entry const& e ) const;
        std::vector<Entry> LoadEntries() const;

        // Rewrites entries.jsonl: drops entries in `del`, renames entries in `edit` to (description, project)
        void RewriteEntries( std::vector<Entry> const& del, std::vector<Entry> const& edit, std::string const& description, std::string const& project ) const;

        // Excel-friendly CSV of every entry, oldest first. Returns its path.
        std::optional<std::filesystem::path> ExportCsv() const;

        std::optional<Status> LoadStatus() const;
        void SaveStatus( Status const& s ) const;

        // Programs the tracker has seen in front lately, newest first
        std::vector<std::string> LoadRecent() const;
        void SaveRecent( std::vector<std::string> const& recent ) const;

        std::optional<Placement> LoadPlacement() const;
        void SavePlacement( Placement const& p ) const;

        private:

        // Entries from entries.jsonl text; bad lines are skipped
        static std::vector<Entry> ParseEntries( std::string const& text );

        std::filesystem::path File( std::string const& name ) const;

        //-------------------------------------------------------------------------

        private:

        std::filesystem::path   m_dir;
    };
}
