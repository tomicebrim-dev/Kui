#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>

#include "Core/Layout.h"
#include "Core/Store.h"
#include "Core/TimeUtil.h"
#include "Platform/Platform.h"
#include "Setup/SetupDetection.h"
#include "Updater/Updater.h"

//-------------------------------------------------------------------------
// Core logic checks, no framework: a failed CHECK prints the line and exits non-zero. Run by ctest.
//-------------------------------------------------------------------------

#define CHECK( expr ) \
    do \
    { \
        if( !( expr ) ) \
        { \
            std::printf( "FAILED %s:%d  %s\n", __FILE__, __LINE__, #expr ); \
            std::exit( 1 ); \
        } \
    } \
    while( false )

namespace Kui::Tests
{
    static Core::Rule MakeRule( char const* pKeyword, Core::Mode mode )
    {
        Core::Rule rule;
        rule.m_id = 1;
        rule.m_keyword = pKeyword;
        rule.m_mode = mode;
        return rule;
    }

    static Core::Entry MakeEntry( int64_t start, int64_t end, char const* pDescription = "", char const* pProject = "" )
    {
        return Core::Entry{ start, end, pDescription, pProject, false };
    }

    //-------------------------------------------------------------------------

    static void Matching()
    {
        std::vector<Core::Rule> rules = { MakeRule( "visual studio", Core::Mode::Phrase ) };
        CHECK( Core::FindRule( rules, "devenv", "Solution1 - Microsoft Visual Studio" ) != nullptr );
        CHECK( Core::FindRule( rules, "Code", "studio visual" ) == nullptr );

        rules = { MakeRule( "studio visual", Core::Mode::AllWords ) };
        CHECK( Core::FindRule( rules, "devenv", "Microsoft Visual Studio" ) != nullptr );

        rules = { MakeRule( "  ", Core::Mode::Phrase ) };
        CHECK( Core::FindRule( rules, "a", "b" ) == nullptr );

        rules = { MakeRule( "code", Core::Mode::Phrase ) };
        rules[0].m_enabled = false;
        CHECK( Core::FindRule( rules, "Code", "" ) == nullptr );
    }

    static void Recent()
    {
        std::vector<std::string> list;
        CHECK( Core::Store::BumpRecent( list, "a" ) && Core::Store::BumpRecent( list, "b" ) && Core::Store::BumpRecent( list, "a" ) );
        CHECK( !Core::Store::BumpRecent( list, "a" ) && !Core::Store::BumpRecent( list, "" ) );
        CHECK( ( list == std::vector<std::string>{ "a", "b" } ) );
        for( int i = 0; i < 30; i++ )
        {
            Core::Store::BumpRecent( list, std::to_string( i ) );
        }
        CHECK( list.size() == Core::Store::s_recentMax );
    }

    static void Vdf()
    {
        std::string const text = "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\SteamLibrary\"\n\t\t\"label\"\t\t\"\"\n\t}\n}";
        CHECK( ( Setup::VdfValues( text, "path" ) == std::vector<std::string>{ "D:\\SteamLibrary" } ) );
        CHECK( ( Setup::VdfValues( "\t\"name\"\t\t\"Hades II\"", "name" ) == std::vector<std::string>{ "Hades II" } ) );
        CHECK( Setup::VdfValues( "\"libraryfolders\"", "path" ).empty() );
    }

    static void Lanes()
    {
        // a,b overlap -> 2 lanes; c overlaps b only (a ended) -> reuses lane 0; d is its own cluster
        std::vector<std::pair<size_t, size_t>> const expected = { { 0, 2 }, { 1, 2 }, { 0, 2 }, { 0, 1 } };
        CHECK( Core::Lanes( { { 0, 10 }, { 5, 20 }, { 12, 15 }, { 30, 40 } } ) == expected );
    }

    static void Span()
    {
        std::vector<Core::Entry> const entries = { MakeEntry( 500000, 500100 ), MakeEntry( 200000, 200050 ), MakeEntry( 99990, 100010 ), MakeEntry( 10, 20 ) };
        std::vector<Core::Entry const*> const found = Core::Span( entries, 100000, 300000, 100 );
        CHECK( found.size() == 2 && found[0]->m_start == 200000 && found[1]->m_start == 99990 );
        CHECK( Core::Span( entries, 600000, 700000, 100 ).empty() );

        // A 3-day entry still shows on its last day
        std::vector<Core::Entry> const long_ = { MakeEntry( 0, 3 * 86400 ) };
        CHECK( Core::Span( long_, 2 * 86400, 3 * 86400, 3 * 86400 ).size() == 1 );
    }

    // Rename and delete through the real files, in a temp folder
    static void StoreRoundTrip()
    {
        std::filesystem::path const dir = std::filesystem::temp_directory_path() / std::format( "kui-test-{}", GetCurrentProcessId() );
        std::filesystem::remove_all( dir );
        Core::Store const store( dir );

        int64_t const t = Core::DayStart( Core::DayOf( Core::Now() ) ) + 3600;
        std::vector<Core::Entry> const entries = { MakeEntry( t, t + 60, "a", "P" ), MakeEntry( t + 100, t + 160, "b", "P" ), MakeEntry( t + 200, t + 260, "a", "P" ) };
        for( Core::Entry const& entry : entries )
        {
            store.AppendEntry( entry );
        }
        CHECK( store.LoadEntries().size() == 3 );

        // Rename both "a" pieces
        store.RewriteEntries( {}, { entries[0], entries[2] }, "c", "Q" );
        std::vector<Core::Entry> loaded = store.LoadEntries();
        CHECK( loaded.size() == 3 );
        CHECK( loaded[0].m_description == "c" && loaded[0].m_project == "Q" && loaded[1].m_description == "b" && loaded[2].m_description == "c" );

        // Delete one, then put it back (what undo does)
        store.RewriteEntries( { loaded[1] }, {}, "", "" );
        CHECK( store.LoadEntries().size() == 2 );
        store.AppendEntry( loaded[1] );
        CHECK( store.LoadEntries().size() == 3 );

        // Timer start/stop logs an entry
        store.StartTimer( Core::Timer{ t + 1000, "run", "", std::nullopt } );
        CHECK( store.LoadTimer().has_value() );
        store.StopTimer( t + 1300 );
        CHECK( !store.LoadTimer().has_value() );
        loaded = store.LoadEntries();
        CHECK( loaded.size() == 4 && loaded.back().m_description == "run" && loaded.back().m_end - loaded.back().m_start == 300 );

        // Settings round trip
        Core::Settings settings;
        settings.m_rules = { MakeRule( "excel budget", Core::Mode::AllWords ) };
        settings.m_workHours = 7.5f;
        settings.m_textScale = 1.4f;
        settings.m_checkUpdates = false;
        settings.m_autostart = false;
        settings.m_projectColors = { { "Recording", 0xff3366ccu } };
        store.SaveSettings( settings );
        Core::Settings back = store.LoadSettings();
        CHECK( back.m_rules.size() == 1 && back.m_rules[0].m_mode == Core::Mode::AllWords && back.m_workHours == 7.5f );
        CHECK( back.m_textScale == 1.4f && !back.m_checkUpdates && !back.m_autostart );
        CHECK( back.m_projectColors.size() == 1 && back.m_projectColors["Recording"] == 0xff3366ccu );

        // A save while the other process is reading the file waits for the read instead of being dropped
        {
            std::ifstream reader( dir / "settings.json" );
            std::thread closer( [&reader] { std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) ); reader.close(); } );
            settings.m_workHours = 6.0f;
            store.SaveSettings( settings );
            closer.join();
        }
        back = store.LoadSettings();
        CHECK( back.m_workHours == 6.0f );

        std::filesystem::remove_all( dir );
    }

    static void Versions()
    {
        CHECK( Updater::IsNewer( "v1.0.1", "1.0.0" ) );
        CHECK( Updater::IsNewer( "1.10.0", "1.9.9" ) );
        CHECK( Updater::IsNewer( "2", "1.9" ) );
        CHECK( !Updater::IsNewer( "1.0", "1.0.0" ) );
        CHECK( !Updater::IsNewer( "v1.0.0", "1.0.0" ) );
        CHECK( !Updater::IsNewer( "1.0.0-beta", "1.0.0" ) );
    }

    // Unsigned and tampered files have no signer; an intact signed one does
    static void Signer()
    {
        wchar_t self[MAX_PATH];
        GetModuleFileNameW( nullptr, self, MAX_PATH );
        CHECK( !Updater::SignerOf( self ).has_value() );

        // ponytail: needs an embedded-signed exe that ships with Windows; skipped where Defender's stub is missing
        std::filesystem::path const stub = L"C:\\Windows\\System32\\MpSigStub.exe";
        if( !std::filesystem::exists( stub ) )
        {
            return;
        }

        std::filesystem::path const copy = std::filesystem::temp_directory_path() / std::format( "kui-signed-{}.exe", GetCurrentProcessId() );
        std::filesystem::copy_file( stub, copy, std::filesystem::copy_options::overwrite_existing );
        CHECK( Updater::SignerOf( copy.wstring() ) == Updater::SignerOf( stub.wstring() ) && Updater::SignerOf( copy.wstring() ).has_value() );
        {
            std::fstream file( copy, std::ios::in | std::ios::out | std::ios::binary );
            file.seekg( 1024 );
            char const byte = static_cast<char>( file.get() ^ 0xFF );
            file.seekp( 1024 );
            file.put( byte );
        }
        CHECK( !Updater::SignerOf( copy.wstring() ).has_value() );
        std::filesystem::remove( copy );
    }

    //-------------------------------------------------------------------------

    static void AntiCheatNames()
    {
        CHECK( Platform::IsAntiCheatName( "EasyAntiCheat" ) );
        CHECK( Platform::IsAntiCheatName( "BEService" ) );
        CHECK( Platform::IsAntiCheatName( "vgc" ) );
        CHECK( !Platform::IsAntiCheatName( "chrome" ) );
        CHECK( !Platform::IsAntiCheatName( "" ) );
    }

    //-------------------------------------------------------------------------

    // Anti-cheat safety: Kui runs beside protected games, so Source/ must never touch another process.
    // These are what anti-cheats flag: handles into other processes, memory access, injection, hooks, input sniffing, debug privileges.
    // ponytail: text scan, drops everything after "//" on a line; a call hidden behind a macro or GetProcAddress slips through.
    static void NoAntiCheatTriggers()
    {
        static char const* const forbidden[] =
        {
            "OpenProcess", "NtOpenProcess", "DuplicateHandle", "NtQueryInformationProcess",
            "ReadProcessMemory", "WriteProcessMemory", "VirtualAllocEx", "VirtualProtectEx", "VirtualQueryEx", "CreateRemoteThread",
            "SetThreadContext", "SuspendThread", "DebugActiveProcess", "SeDebugPrivilege", "AdjustTokenPrivileges",
            "TH32CS_SNAPMODULE", "TH32CS_SNAPTHREAD", "TH32CS_SNAPHEAPLIST", "Module32", "Thread32", "Heap32", "EnumProcessModules",
            "SetWindowsHookEx", "SetWinEventHook", "GetAsyncKeyState", "GetKeyboardState", "RegisterRawInputDevices", "GetRawInputData",
            "SendInput", "keybd_event", "mouse_event",
        };

        std::filesystem::path const root = std::filesystem::path( KUI_SOURCE_DIR ) / "Source";
        CHECK( std::filesystem::exists( root ) );

        int files = 0;
        for( auto const& entry : std::filesystem::recursive_directory_iterator( root ) )
        {
            std::string const ext = entry.path().extension().string();
            if( !entry.is_regular_file() || ( ext != ".cpp" && ext != ".h" ) )
            {
                continue;
            }

            files++;
            std::ifstream in( entry.path() );
            std::string line;
            for( int n = 1; std::getline( in, line ); n++ )
            {
                line = line.substr( 0, line.find( "//" ) );
                for( char const* pName : forbidden )
                {
                    if( line.find( pName ) != std::string::npos )
                    {
                        std::printf( "Anti-cheat risk: %s uses %s (line %d)\n", entry.path().string().c_str(), pName, n );
                        CHECK( false );
                    }
                }
            }
        }
        CHECK( files > 10 ); // The scan actually found the sources
    }
}

int main()
{
    Kui::Tests::Matching();
    Kui::Tests::Recent();
    Kui::Tests::Vdf();
    Kui::Tests::Lanes();
    Kui::Tests::Span();
    Kui::Tests::StoreRoundTrip();
    Kui::Tests::Versions();
    Kui::Tests::Signer();
    Kui::Tests::AntiCheatNames();
    Kui::Tests::NoAntiCheatTriggers();
    std::printf( "All core tests passed\n" );
    return 0;
}
