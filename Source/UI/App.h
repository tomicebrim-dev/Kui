#pragma once

#include <compare>
#include <cstdint>
#include <future>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "Core/Store.h"
#include "Core/TimeUtil.h"
#include "Setup/SetupDetection.h"
#include "Updater/Updater.h"

//-------------------------------------------------------------------------
// The main window's state and every view. Each view lives in its own App_*.cpp file.
//-------------------------------------------------------------------------

namespace Kui::UI
{
    enum class View
    {
        Timer,
        Calendar,
        Timeline,
        Reports,
        Programs,
    };

    // How the window starts: the page to show and the test hooks
    struct StartOptions
    {
        bool    m_isScreenshot = false;     // Demo data, no writes to the real settings or Run key
        View    m_view = View::Timer;
        int     m_reportTab = 0;            // 0 = Summary, 1 = Workload
        bool    m_showSetup = false;
        bool    m_showSettings = false;     // Open the settings popup
        int     m_setupStep = 0;
    };

    // Same activity (description + project) on one day: shown as one row, expandable
    struct GroupKey
    {
        auto operator<=>( GroupKey const& ) const = default;

        Core::Day       m_day = 0;
        std::string     m_description;
        std::string     m_project;
    };

    struct Group
    {
        GroupKey                m_key;
        bool                    m_isAuto = true;
        std::vector<size_t>     m_items;        // Indices into App::m_entries, newest first
        int64_t                 m_total = 0;
        int64_t                 m_first = 0;
        int64_t                 m_last = 0;
    };

    // Per-project seconds, biggest first
    using ProjectSecs = std::vector<std::pair<std::string, int64_t>>;

    //-------------------------------------------------------------------------

    class App
    {
        public:

        App( Core::Store store, StartOptions const& options );

        // Builds one ImGui frame covering the whole window
        void Draw();

        private:

        // Small files are re-read every call; entries.jsonl (the big one) only when it changed on disk
        void Reload( bool force );
        void Save() const;

        void Start( std::string description, std::string project );
        void Stop();
        void Toggle();
        void Delete( std::vector<Core::Entry> entries );

        // Puts entries on another project; undoable like a delete
        void MoveToProject( std::vector<Core::Entry> entries, std::string const& project );
        void UndoDelete();
        void ShowToast( std::string message );

        // Asks GitHub for the latest release in the background; PollUpdate() picks up the answer
        void CheckForUpdate();

        // Downloads and runs the newer release's installer in the background, then quits
        void InstallUpdate();
        void PollUpdate();

        // Adds a rule unless the keyword is empty or already tracked. False when skipped. Caller saves.
        bool AddRule( std::string const& keyword, std::string const& description, std::string const& project );

        // Seconds on the running timer, 0 when stopped
        int64_t LiveSecs() const;

        // Per-project seconds on a day, including the running timer
        ProjectSecs DayParts( Core::Day day ) const;

        void HandleShortcuts();
        void DrawRail();
        void DrawSidebar();
        void DrawSidebarBody();
        void DrawSidebarFooter();
        void DrawTopBar();
        bool DrawWatchButton();
        void DrawSuggestions();
        void DrawEntries();
        void DrawWeekNav( Core::Day today );
        void DrawCalendar();
        void DrawTimeline();
        void DrawReports();
        void DrawPrograms();
        void DrawSetup();
        // The settings popup, centered over the window. Opened by m_openSettings.
        void DrawSettings();
        void DrawToast();

        //-------------------------------------------------------------------------

        private:

        struct Activity
        {
            std::string     m_description;
            std::string     m_project;
        };

        struct Edit
        {
            GroupKey        m_key;
            std::string     m_description;
            std::string     m_project;
            bool            m_needsFocus = true;    // Focus the name box on the first frame only
        };

        struct Undo
        {
            std::vector<Core::Entry>    m_entries;
            double                      m_time = 0;
            bool                        m_replace = false;  // Drop the matching entries first (undoing a move, not a delete)
        };

        // A timeline bar being dragged onto another project's row
        struct TimelineDrag
        {
            std::vector<Core::Entry>    m_entries;          // Copies, so a reload mid-drag leaves nothing dangling
            std::string                 m_project;
            std::string                 m_label;
            Core::Day                   m_from = 0;         // The bar's first day
            float                       m_grabX = 0.0f;     // Mouse x within the bar
            float                       m_width = 0.0f;
        };

        struct Toast
        {
            std::string     m_message;
            double          m_time = 0;
        };

        struct SetupState
        {
            int                 m_step = 0;
            Setup::Detected     m_detected;
        };

        Core::Store                                         m_store;
        bool                                                m_isScreenshot = false;

        // Files
        Core::Settings                                      m_settings;
        std::optional<Core::Timer>                          m_timer;
        std::vector<Core::Entry>                            m_entries;      // Newest first
        int64_t                                             m_longestEntry = 0; // Secs, for Core::Span
        std::optional<Core::Status>                         m_status;
        std::vector<std::string>                            m_recent;
        std::optional<std::filesystem::file_time_type>      m_entriesStamp;
        double                                              m_checkedAt = -1000.0;

        // Caches rebuilt only when entries.jsonl changes
        std::unordered_map<Core::Day, int64_t>              m_daySecs;
        std::vector<Group>                                  m_groups;
        std::vector<std::string>                            m_projects;
        std::vector<Activity>                               m_activities;

        // UI state
        View                                                m_view = View::Timer;
        std::string                                         m_description;
        std::string                                         m_project;
        std::vector<std::string>                            m_openPrograms;
        std::string                                         m_manualProgram;
        int                                                 m_week = 0;
        int                                                 m_calendarDays = 7;
        bool                                                m_calendarScrolled = false;
        int                                                 m_timelineWeeks = 2;
        bool                                                m_timelineByActivity = false;   // Rows per activity instead of per project
        std::optional<TimelineDrag>                         m_timelineDrag;
        int                                                 m_reportTab = 0;
        std::set<GroupKey>                                  m_expanded;
        std::set<Core::Day>                                 m_collapsedDays;    // Day headers folded shut
        std::optional<Edit>                                 m_edit;
        std::optional<Undo>                                 m_undo;
        std::optional<Toast>                                m_toast;
        bool                                                m_focusDescription = false;
        bool                                                m_suggestionsHovered = false;
        float                                               m_sideFooterHeight = 0.0f;
        std::optional<SetupState>                           m_setup;
        bool                                                m_openSettings = false; // Open the settings popup next frame
        bool                                                m_settingsOpen = false; // Settings popup showing (last frame)
        std::string                                         m_exportStatus;     // Result of the last CSV export

        // Updates
        std::future<std::optional<Updater::Release>>        m_updateCheck;
        std::future<bool>                                   m_updateInstall;
        std::optional<Updater::Release>                     m_update;       // A newer release, once found
        std::string                                         m_updateStatus;
    };
}
