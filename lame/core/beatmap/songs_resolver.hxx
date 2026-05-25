#pragma once

#include <impl/memory/process.hxx>
#include <Windows.h>
#include <ShlObj.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <Lmcons.h>

namespace beatmap::songs {

    inline std::wstring get_process_exe_directory( const memory::c_process& process ) {
        if ( !process.valid( ) )
            return L"";

        wchar_t path[ MAX_PATH ]{};
        DWORD size = MAX_PATH;
        if ( !QueryFullProcessImageNameW( process.handle( ), 0, path, &size ) )
            return L"";

        std::wstring exe = path;
        const auto slash = exe.find_last_of( L"\\/" );
        if ( slash != std::wstring::npos )
            exe.resize( slash );
        return exe;
    }

    inline std::wstring local_appdata_osu( ) {
        wchar_t local_app_data[ MAX_PATH ]{};
        if ( FAILED( SHGetFolderPathW( nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, local_app_data ) ) )
            return L"";

        std::wstring path = local_app_data;
        path += L"\\osu";
        return path;
    }

    inline std::wstring roaming_appdata_osu( ) {
        wchar_t app_data[ MAX_PATH ]{};
        if ( FAILED( SHGetFolderPathW( nullptr, CSIDL_APPDATA, nullptr, 0, app_data ) ) )
            return L"";

        std::wstring path = app_data;
        path += L"\\osu";
        return path;
    }

    inline std::wstring local_appdata_osulazer_current( ) {
        wchar_t local_app_data[ MAX_PATH ]{};
        if ( FAILED( SHGetFolderPathW( nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, local_app_data ) ) )
            return L"";

        std::wstring path = local_app_data;
        path += L"\\osulazer\\current";
        return path;
    }

    inline std::wstring local_appdata_osu_songs( ) {
        wchar_t local_app_data[ MAX_PATH ]{};
        if ( FAILED( SHGetFolderPathW( nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, local_app_data ) ) )
            return L"";

        std::wstring path = local_app_data;
        path += L"\\osu!\\Songs";
        return path;
    }

    inline std::wstring trim( std::wstring s ) {
        while ( !s.empty( ) && ( s.back( ) == L' ' || s.back( ) == L'\r' || s.back( ) == L'\n' || s.back( ) == L'\t' ) )
            s.pop_back( );
        size_t start = 0;
        while ( start < s.size( ) && ( s[ start ] == L' ' || s[ start ] == L'\t' ) )
            ++start;
        return s.substr( start );
    }

    inline bool is_absolute_path( const std::wstring& path ) {
        return path.size( ) >= 2 && path[ 1 ] == L':';
    }

    inline std::wstring read_beatmap_directory_from_cfg( const std::wstring& install_dir ) {
        wchar_t username[ UNLEN + 1 ]{};
        DWORD username_len = UNLEN + 1;
        if ( !GetUserNameW( username, &username_len ) )
            return L"";

        const std::wstring cfg_path = install_dir + L"\\osu!." + username + L".cfg";
        std::wifstream file( cfg_path );
        if ( !file )
            return L"";

        std::wstring line;
        while ( std::getline( file, line ) ) {
            const auto eq = line.find( L'=' );
            if ( eq == std::wstring::npos )
                continue;

            auto key = trim( line.substr( 0, eq ) );
            auto value = trim( line.substr( eq + 1 ) );

            if ( _wcsicmp( key.c_str( ), L"BeatmapDirectory" ) == 0 )
                return value;
        }

        return L"";
    }

    inline std::wstring resolve_beatmap_directory( const std::wstring& install_dir, const std::wstring& cfg_value ) {
        if ( cfg_value.empty( ) )
            return install_dir + L"\\Songs";

        if ( is_absolute_path( cfg_value ) )
            return cfg_value;

        if ( cfg_value.find( L'\\' ) != std::wstring::npos || cfg_value.find( L'/' ) != std::wstring::npos )
            return install_dir + L"\\" + cfg_value;

        return install_dir + L"\\" + cfg_value;
    }

    inline std::wstring resolve( const memory::c_process& process ) {
        std::vector<std::wstring> candidates;

        const auto install_dir = get_process_exe_directory( process );
        if ( !install_dir.empty( ) ) {
            const auto cfg_value = read_beatmap_directory_from_cfg( install_dir );
            if ( !cfg_value.empty( ) )
                candidates.push_back( resolve_beatmap_directory( install_dir, cfg_value ) );

            candidates.push_back( install_dir + L"\\Songs" );
        }

        const auto appdata_songs = local_appdata_osu_songs( );
        if ( !appdata_songs.empty( ) )
            candidates.push_back( appdata_songs );

        for ( const auto& path : candidates ) {
            if ( path.empty( ) )
                continue;
            if ( std::filesystem::exists( path ) && std::filesystem::is_directory( path ) ) {
                return path;
            }
        }

        return appdata_songs;
    }

    inline std::wstring resolve_lazer_data_dir( const memory::c_process& process ) {
        std::vector<std::wstring> candidates;

        const auto roaming_osu = roaming_appdata_osu( );
        if ( !roaming_osu.empty( ) )
            candidates.push_back( roaming_osu );

        const auto local_osu = local_appdata_osu( );
        if ( !local_osu.empty( ) )
            candidates.push_back( local_osu );

        const auto osulazer_current = local_appdata_osulazer_current( );
        if ( !osulazer_current.empty( ) )
            candidates.push_back( osulazer_current );

        const auto exe_dir = get_process_exe_directory( process );
        if ( !exe_dir.empty( ) )
            candidates.push_back( exe_dir );

        std::wstring best_path;
        size_t best_file_count = 0;

        for ( const auto& path : candidates ) {
            if ( path.empty( ) )
                continue;

            const auto files_dir = std::filesystem::path( path ) / L"files";
            if ( !std::filesystem::exists( files_dir ) || !std::filesystem::is_directory( files_dir ) )
                continue;

            size_t file_count = 0;
            try {
                for ( const auto& entry : std::filesystem::recursive_directory_iterator( files_dir, std::filesystem::directory_options::skip_permission_denied ) ) {
                    if ( entry.is_regular_file( ) )
                        ++file_count;
                }
            }
            catch ( ... ) {
                continue;
            }

            if ( file_count > best_file_count ) {
                best_file_count = file_count;
                best_path = path;
            }
        }

        if ( !best_path.empty( ) ) {
            return best_path;
        }

        return roaming_osu;
    }

}