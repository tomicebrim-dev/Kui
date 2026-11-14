#include "Updater/Updater.h"

#include <windows.h>
#include <softpub.h>
#include <winhttp.h>
#include <wintrust.h>

#include <nlohmann/json.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

#include "Core/Text.h"
#include "Platform/Platform.h"

//-------------------------------------------------------------------------

namespace Kui::Updater
{
    constexpr wchar_t g_latestUrl[] = L"https://api.github.com/repos/tomicebrim-dev/Kui/releases/latest";
    constexpr int g_timeoutMs = 10000;

    struct HandleCloser
    {
        void operator()( HINTERNET h ) const { WinHttpCloseHandle( h ); }
    };

    using Handle = std::unique_ptr<void, HandleCloser>;

    //-------------------------------------------------------------------------

    // HTTPS-only GET, following redirects. The body on a 200, else nullopt.
    static std::optional<std::string> HttpGet( std::wstring const& url )
    {
        // Null buffers + -1 lengths: the parts point into `url`
        URL_COMPONENTS parts = { sizeof( parts ) };
        parts.dwHostNameLength = static_cast<DWORD>( -1 );
        parts.dwUrlPathLength = static_cast<DWORD>( -1 );
        parts.dwExtraInfoLength = static_cast<DWORD>( -1 );
        if( !WinHttpCrackUrl( url.c_str(), 0, 0, &parts ) || parts.nScheme != INTERNET_SCHEME_HTTPS )
        {
            return std::nullopt;
        }

        // The query string follows the path in `url`, so one span covers both
        std::wstring const host( parts.lpszHostName, parts.dwHostNameLength );
        std::wstring const path( parts.lpszUrlPath, parts.dwUrlPathLength + parts.dwExtraInfoLength );

        Handle const session( WinHttpOpen( L"Kui-Updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0 ) );
        if( !session )
        {
            return std::nullopt;
        }
        WinHttpSetTimeouts( session.get(), g_timeoutMs, g_timeoutMs, g_timeoutMs, g_timeoutMs );

        Handle const connection( WinHttpConnect( session.get(), host.c_str(), parts.nPort, 0 ) );
        Handle const request( connection ? WinHttpOpenRequest( connection.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE ) : nullptr );
        if( !request || !WinHttpSendRequest( request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0 ) || !WinHttpReceiveResponse( request.get(), nullptr ) )
        {
            return std::nullopt;
        }

        DWORD status = 0;
        DWORD size = sizeof( status );
        WinHttpQueryHeaders( request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX );
        if( status != 200 )
        {
            return std::nullopt;
        }

        std::string body;
        for( ;; )
        {
            DWORD available = 0;
            if( !WinHttpQueryDataAvailable( request.get(), &available ) )
            {
                return std::nullopt;
            }

            if( available == 0 )
            {
                break;
            }

            size_t const offset = body.size();
            body.resize( offset + available );
            DWORD read = 0;
            if( !WinHttpReadData( request.get(), body.data() + offset, available, &read ) )
            {
                return std::nullopt;
            }
            body.resize( offset + read );
        }
        return body;
    }

    static std::array<int, 4> ParseVersion( std::string_view v )
    {
        if( !v.empty() && ( v.front() == 'v' || v.front() == 'V' ) )
        {
            v.remove_prefix( 1 );
        }

        std::array<int, 4> numbers = {};
        size_t i = 0;
        for( char const c : v )
        {
            if( c == '.' )
            {
                if( ++i == numbers.size() )
                {
                    break;
                }
                continue;
            }

            if( c < '0' || c > '9' )
            {
                break;
            }
            numbers[i] = numbers[i] * 10 + ( c - '0' );
        }
        return numbers;
    }

    //-------------------------------------------------------------------------

    char const* CurrentVersion()
    {
        return KUI_VERSION;
    }

    bool IsNewer( std::string_view a, std::string_view b )
    {
        return ParseVersion( a ) > ParseVersion( b );
    }

    std::optional<std::vector<unsigned char>> SignerOf( std::wstring const& file )
    {
        WINTRUST_FILE_INFO fileInfo = { sizeof( fileInfo ) };
        fileInfo.pcwszFilePath = file.c_str();

        WINTRUST_DATA data = { sizeof( data ) };
        data.dwUIChoice = WTD_UI_NONE;
        data.fdwRevocationChecks = WTD_REVOKE_NONE;
        data.dwUnionChoice = WTD_CHOICE_FILE;
        data.pFile = &fileInfo;
        data.dwStateAction = WTD_STATEACTION_VERIFY;
        data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;

        GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        LONG const result = WinVerifyTrust( nullptr, &action, &data );

        // Untrusted root/chain is fine (self-signed): callers pin the cert itself. Bad digest or unsigned is not.
        std::optional<std::vector<unsigned char>> signer;
        CRYPT_PROVIDER_DATA const* pProvider = WTHelperProvDataFromStateData( data.hWVTStateData );
        bool const isChainOnly = result == CERT_E_UNTRUSTEDROOT || result == CERT_E_CHAINING;
        bool const isIntact = pProvider != nullptr && pProvider->cdwTrustStepErrors > TRUSTERROR_STEP_FINAL_SIGPROV
            && pProvider->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_OBJPROV] == ERROR_SUCCESS
            && pProvider->padwTrustStepErrors[TRUSTERROR_STEP_FINAL_SIGPROV] == ERROR_SUCCESS;
        if( result == ERROR_SUCCESS || ( isChainOnly && isIntact ) )
        {
            CRYPT_PROVIDER_SGNR const* pSigner = WTHelperGetProvSignerFromChain( const_cast<CRYPT_PROVIDER_DATA*>( pProvider ), 0, FALSE, 0 );
            if( pSigner != nullptr && pSigner->csCertChain > 0 && pSigner->pasCertChain[0].pCert != nullptr )
            {
                CERT_CONTEXT const* pCert = pSigner->pasCertChain[0].pCert;
                signer.emplace( pCert->pbCertEncoded, pCert->pbCertEncoded + pCert->cbCertEncoded );
            }
        }

        data.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust( nullptr, &action, &data );
        return signer;
    }

    std::optional<Release> FetchLatest()
    {
        std::optional<std::string> const body = HttpGet( g_latestUrl );
        if( !body.has_value() )
        {
            return std::nullopt;
        }

        nlohmann::json const j = nlohmann::json::parse( *body, nullptr, false );
        if( j.is_discarded() || !j.is_object() )
        {
            return std::nullopt;
        }

        auto const tag = j.find( "tag_name" );
        if( tag == j.end() || !tag->is_string() )
        {
            return std::nullopt;
        }

        Release release;
        release.m_version = tag->get<std::string>();
        if( !release.m_version.empty() && ( release.m_version.front() == 'v' || release.m_version.front() == 'V' ) )
        {
            release.m_version.erase( 0, 1 );
        }

        auto const assets = j.find( "assets" );
        if( assets == j.end() || !assets->is_array() )
        {
            return release;
        }

        for( auto const& asset : *assets )
        {
            if( !asset.is_object() )
            {
                continue;
            }

            auto const name = asset.find( "name" );
            auto const url = asset.find( "browser_download_url" );
            if( name != asset.end() && url != asset.end() && name->is_string() && url->is_string() && name->get<std::string>().ends_with( ".msi" ) )
            {
                release.m_msiUrl = url->get<std::string>();
                break;
            }
        }
        return release;
    }

    bool Install( Release const& release )
    {
        std::optional<std::string> const msi = release.m_msiUrl.empty() ? std::nullopt : HttpGet( Core::Widen( release.m_msiUrl ) );
        if( !msi.has_value() )
        {
            return false;
        }

        // Fixed name: nothing from the server ends up on the command line
        std::filesystem::path const path = std::filesystem::temp_directory_path() / "Kui-update.msi";
        std::ofstream file( path, std::ios::binary | std::ios::trunc );
        file.write( msi->data(), static_cast<std::streamsize>( msi->size() ) );
        file.close();
        if( !file )
        {
            return false;
        }

        // Signed by the same cert as this exe, or no install: a hijacked release or download can't run code.
        // ponytail: unsigned (dev) builds skip the check; make it mandatory once every release is signed.
        if( std::optional<std::vector<unsigned char>> const mine = SignerOf( Platform::ExePath() ); mine && SignerOf( path.wstring() ) != mine )
        {
            std::error_code ec;
            std::filesystem::remove( path, ec );
            return false;
        }

        // Where the per-user MSI puts kui.exe (Installer/Package.wxs)
        wchar_t* pLocalAppData = nullptr;
        size_t length = 0;
        _wdupenv_s( &pLocalAppData, &length, L"LOCALAPPDATA" );
        std::filesystem::path const exe = std::filesystem::path( pLocalAppData ? pLocalAppData : L"." ) / L"Programs" / L"Kui" / L"kui.exe";
        std::free( pLocalAppData );

        // cmd strips the outer quotes. Waits for msiexec, then reopens Kui only if the install succeeded.
        std::wstring command = L"cmd.exe /c \"start \"\" /wait msiexec /i \"" + path.wstring() + L"\" /passive && start \"\" \"" + exe.wstring() + L"\"\"";
        STARTUPINFOW startup = { sizeof( startup ) };
        PROCESS_INFORMATION process = {};
        if( !CreateProcessW( nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process ) )
        {
            return false;
        }

        CloseHandle( process.hThread );
        CloseHandle( process.hProcess );
        return true;
    }
}
