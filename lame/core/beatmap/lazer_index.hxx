#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <wincrypt.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <cctype>
#include <algorithm>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace beatmap {

    class c_lazer_index {
    public:
        void invalidate( ) {
            std::unique_lock lock( m_mutex );
            m_by_map_id.clear( );
            m_by_hash.clear( );
            m_by_md5.clear( );
            m_by_set_version.clear( );
            m_data_dir.clear( );
            m_ready = false;
        }

        void ensure_built( const std::wstring& data_dir ) {
            if ( data_dir.empty( ) )
                return;

            {
                std::shared_lock lock( m_mutex );
                if ( m_ready && m_data_dir == data_dir )
                    return;
            }

            build( data_dir );
        }

        [[nodiscard]] std::wstring find_by_hash( const std::string& hash ) const {
            if ( hash.empty( ) )
                return L"";
            std::shared_lock lock( m_mutex );
            const auto normalized = normalize_hash( hash );
            
            const auto it_md5 = m_by_md5.find( normalized );
            if ( it_md5 != m_by_md5.end( ) )
                return it_md5->second;

            const auto it = m_by_hash.find( normalized );
            return it != m_by_hash.end( ) ? it->second : L"";
        }

        [[nodiscard]] std::wstring find_by_map_id( int32_t map_id ) const {
            if ( map_id <= 0 )
                return L"";
            std::shared_lock lock( m_mutex );
            const auto it = m_by_map_id.find( map_id );
            return it != m_by_map_id.end( ) ? it->second : L"";
        }

        [[nodiscard]] std::wstring find_by_set_and_version( int32_t set_id, const std::string& version ) const {
            if ( set_id <= 0 || version.empty( ) )
                return L"";
            std::shared_lock lock( m_mutex );
            const auto it = m_by_set_version.find( make_set_version_key( set_id, version ) );
            return it != m_by_set_version.end( ) ? it->second : L"";
        }

    private:
        mutable std::shared_mutex m_mutex;
        std::wstring m_data_dir;
        std::atomic<bool> m_ready{ false };

        std::unordered_map<int32_t, std::wstring> m_by_map_id;
        std::unordered_map<std::string, std::wstring> m_by_hash;
        std::unordered_map<std::string, std::wstring> m_by_md5;
        std::unordered_map<uint64_t, std::wstring> m_by_set_version;

        static std::string normalize_hash( std::string hash ) {
            std::transform( hash.begin( ), hash.end( ), hash.begin( ),
                []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );
            return hash;
        }

        static uint64_t make_set_version_key( int32_t set_id, const std::string& version ) {
            uint64_t key = static_cast<uint32_t>( set_id );
            key <<= 32;
            uint32_t h = 2166136261u;
            for ( unsigned char c : version ) {
                h ^= c;
                h *= 16777619u;
            }
            key |= static_cast<uint64_t>( h );
            return key;
        }

        static std::string trim( std::string s ) {
            while ( !s.empty( ) && ( s.back( ) == ' ' || s.back( ) == '\r' || s.back( ) == '\n' ) )
                s.pop_back( );
            size_t start = 0;
            while ( start < s.size( ) && s[ start ] == ' ' )
                ++start;
            return s.substr( start );
        }

        static bool parse_osu_header(
            const std::filesystem::path& path,
            int32_t& map_id,
            int32_t& set_id,
            std::string& version ) {
            std::ifstream file( path, std::ios::binary );
            if ( !file )
                return false;

            map_id = 0;
            set_id = 0;
            version.clear( );

            std::string line;
            std::string section;
            size_t lines_read = 0;
            while ( lines_read < 256 && std::getline( file, line ) ) {
                ++lines_read;
                if ( line.empty( ) )
                    continue;
                if ( line.front( ) == '[' ) {
                    section = line;
                    continue;
                }

                const auto colon = line.find( ':' );
                if ( colon == std::string::npos )
                    continue;

                const auto key = trim( line.substr( 0, colon ) );
                const auto value = trim( line.substr( colon + 1 ) );

                if ( key == "BeatmapID" ) {
                    try {
                        map_id = std::stoi( value );
                    }
                    catch ( ... ) {}
                }
                else if ( key == "BeatmapSetID" ) {
                    try {
                        set_id = std::stoi( value );
                    }
                    catch ( ... ) {}
                }
                else if ( key == "Version" && section.find( "General" ) != std::string::npos ) {
                    version = value;
                }

                if ( section == "[HitObjects]" )
                    break;
            }

            return map_id > 0 || set_id > 0 || !version.empty( );
        }

        static std::string calculate_md5( const std::filesystem::path& path ) {
            std::ifstream file( path, std::ios::binary );
            if ( !file )
                return "";

            HCRYPTPROV hProv = 0;
            HCRYPTHASH hHash = 0;
            if ( !CryptAcquireContextW( &hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT ) )
                return "";

            if ( !CryptCreateHash( hProv, CALG_MD5, 0, 0, &hHash ) ) {
                CryptReleaseContext( hProv, 0 );
                return "";
            }

            constexpr size_t buffer_size = 8192;
            std::vector<char> buffer( buffer_size );
            while ( file.good( ) ) {
                file.read( buffer.data( ), buffer_size );
                std::streamsize bytes_read = file.gcount( );
                if ( bytes_read > 0 ) {
                    if ( !CryptHashData( hHash, reinterpret_cast<const BYTE*>( buffer.data( ) ), static_cast<DWORD>( bytes_read ), 0 ) ) {
                        CryptDestroyHash( hHash );
                        CryptReleaseContext( hProv, 0 );
                        return "";
                    }
                }
            }

            DWORD cbHashSize = 16;
            BYTE rgbHash[ 16 ] = { 0 };
            std::string md5;
            if ( CryptGetHashParam( hHash, HP_HASHVAL, rgbHash, &cbHashSize, 0 ) ) {
                char szHex[ 33 ] = { 0 };
                for ( DWORD i = 0; i < 16; ++i ) {
                    sprintf_s( szHex + ( i * 2 ), 3, "%02x", rgbHash[ i ] );
                }
                md5 = szHex;
            }

            CryptDestroyHash( hHash );
            CryptReleaseContext( hProv, 0 );
            return md5;
        }

        void build( const std::wstring& data_dir ) {
            std::unordered_map<int32_t, std::wstring> by_map_id;
            std::unordered_map<std::string, std::wstring> by_hash;
            std::unordered_map<std::string, std::wstring> by_md5;
            std::unordered_map<uint64_t, std::wstring> by_set_version;

            const auto files_root = std::filesystem::path( data_dir ) / L"files";
            size_t indexed = 0;

            if ( std::filesystem::exists( files_root ) ) {
                try {
                    for ( const auto& entry : std::filesystem::recursive_directory_iterator( files_root, std::filesystem::directory_options::skip_permission_denied ) ) {
                        try {
                            if ( !entry.is_regular_file( ) )
                                continue;

                            const auto& path = entry.path( );
                            const auto hash = normalize_hash( path.filename( ).string( ) );
                            
                            if ( hash.size( ) != 64 )
                                continue;

                            const auto wide_path = path.wstring( );
                            by_hash[ hash ] = wide_path;

                            int32_t map_id = 0;
                            int32_t set_id = 0;
                            std::string version;
                            if ( parse_osu_header( path, map_id, set_id, version ) ) {
                                if ( map_id > 0 )
                                    by_map_id[ map_id ] = wide_path;
                                if ( set_id > 0 && !version.empty( ) )
                                    by_set_version[ make_set_version_key( set_id, version ) ] = wide_path;

                                const auto md5 = calculate_md5( path );
                                if ( !md5.empty( ) )
                                    by_md5[ normalize_hash( md5 ) ] = wide_path;
                            }

                            ++indexed;
                        }
                        catch ( ... ) {

                        }
                    }
                }
                catch ( ... ) {

                }
            }

            {
                std::unique_lock lock( m_mutex );
                m_by_map_id = std::move( by_map_id );
                m_by_hash = std::move( by_hash );
                m_by_md5 = std::move( by_md5 );
                m_by_set_version = std::move( by_set_version );
                m_data_dir = data_dir;
                m_ready = true;
            }
        }
    };

}