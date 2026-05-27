#pragma once

#include <core/beatmap/i_beatmap_provider.hxx>
#include <impl/memory/input.hxx>
#include <impl/util/playfield.hxx>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <vector>

namespace beatmap {

    class c_stable_parser : public i_beatmap_provider {
    public:
        void set_songs_path( std::wstring path ) { m_songs_path = std::move( path ); }

        [[nodiscard]] const std::wstring& songs_path( ) const { return m_songs_path; }
        [[nodiscard]] const std::wstring& last_beatmap_path( ) const { return m_last_beatmap_path; }

        bool try_load( memory::c_process& /*process*/, const osu::game_snapshot_t& game, osu::beatmap_data_t& out ) override {
            out = {};
            out.map_id = game.map_id;
            out.set_id = game.set_id;
            out.songs_path = wide_to_utf8( m_songs_path );

            const bool in_load_state =
                game.cur_state == osu::game_state_t::play ||
                game.cur_state == osu::game_state_t::select_play;
            const bool has_map_id = game.map_id != 0;
            const bool has_map_path = !game.map_folder.empty( ) && !game.map_file.empty( );

            if ( !in_load_state || ( !has_map_id && !has_map_path ) )
                return false;

            if ( m_songs_path.empty( ) || !std::filesystem::exists( m_songs_path ) ) {
                out.error = "songs folder not found: " + out.songs_path;
                return false;
            }

            auto path = resolve_map_path( game );
            if ( path.empty( ) ) {
                out.error = "beatmap file not found in " + out.songs_path;
                return false;
            }

            m_last_beatmap_path = path;
            out.beatmap_path = wide_to_utf8( path );

            out.screen_width = 1920;
            out.screen_height = 1080;
            playfield::get_window_size( input::target_window( ), out.screen_width, out.screen_height );

            if ( !parse_file( path, out ) ) {
                out.error = "failed to parse " + out.beatmap_path;
                return false;
            }

            m_last_map_id = game.map_id;
            out.loaded = true;
            return true;
        }

        [[nodiscard]] std::wstring resolve_map_path_for( const osu::game_snapshot_t& game ) const {
            return resolve_map_path( game );
        }

        bool load_from_path( const std::wstring& path, const osu::game_snapshot_t& game, osu::beatmap_data_t& out ) {
            out = {};
            out.map_id = game.map_id;
            out.set_id = game.set_id;
            out.songs_path = wide_to_utf8( m_songs_path );

            if ( path.empty( ) || !std::filesystem::exists( path ) )
                return false;

            m_last_beatmap_path = path;
            out.beatmap_path = wide_to_utf8( path );

            out.screen_width = 1920;
            out.screen_height = 1080;
            playfield::get_window_size( input::target_window( ), out.screen_width, out.screen_height );

            if ( !parse_file( path, out ) )
                return false;

            out.loaded = true;
            return true;
        }

    private:
        std::wstring m_songs_path;
        std::wstring m_last_beatmap_path;
        int32_t m_last_map_id = -1;

        static std::string wide_to_utf8( const std::wstring& wide ) {
            if ( wide.empty( ) )
                return {};
            const int len = WideCharToMultiByte( CP_UTF8, 0, wide.c_str( ), -1, nullptr, 0, nullptr, nullptr );
            if ( len <= 0 )
                return {};
            std::string out( static_cast<size_t>( len - 1 ), '\0' );
            WideCharToMultiByte( CP_UTF8, 0, wide.c_str( ), -1, out.data( ), len, nullptr, nullptr );
            return out;
        }

        static std::wstring trim( std::wstring s ) {
            while ( !s.empty( ) && ( s.back( ) == L' ' || s.back( ) == L'\r' || s.back( ) == L'\n' ) )
                s.pop_back( );
            size_t start = 0;
            while ( start < s.size( ) && s[ start ] == L' ' )
                ++start;
            return s.substr( start );
        }

        static int32_t parse_beatmap_id_line( const std::string& line ) {
            const auto pos = line.find( ':' );
            if ( pos == std::string::npos )
                return 0;
            try {
                return std::stoi( line.substr( pos + 1 ) );
            }
            catch ( ... ) {
                return 0;
            }
        }

        static std::string trim_narrow( std::string s ) {
            while ( !s.empty( ) && ( s.back( ) == ' ' || s.back( ) == '\r' || s.back( ) == '\n' ) )
                s.pop_back( );
            size_t start = 0;
            while ( start < s.size( ) && s[ start ] == ' ' )
                ++start;
            return s.substr( start );
        }

        static std::wstring utf8_to_wide( const std::string& utf8 ) {
            if ( utf8.empty( ) )
                return {};
            const int len = ::MultiByteToWideChar( CP_UTF8, 0, utf8.c_str( ), -1, nullptr, 0 );
            if ( len <= 0 )
                return {};
            std::wstring wide( static_cast<size_t>( len - 1 ), L'\0' );
            ::MultiByteToWideChar( CP_UTF8, 0, utf8.c_str( ), -1, wide.data( ), len );
            return wide;
        }

        static std::vector<std::string> split_csv( const std::string& line ) {
            std::vector<std::string> vars;
            std::stringstream ss( line );
            std::string item;
            while ( std::getline( ss, item, ',' ) )
                vars.push_back( trim_narrow( item ) );
            return vars;
        }

        std::wstring resolve_map_path( const osu::game_snapshot_t& game ) const {
            if ( !game.map_folder.empty( ) && !game.map_file.empty( ) ) {
                const auto folder = utf8_to_wide( game.map_folder );
                const auto file = utf8_to_wide( game.map_file );
                std::filesystem::path direct = m_songs_path;
                direct /= folder;
                direct /= file;
                if ( std::filesystem::is_regular_file( direct ) )
                    return direct.wstring( );
            }

            if ( game.map_id != 0 )
                return find_map_file( game.set_id, game.map_id );

            return L"";
        }

        std::wstring find_map_file( int32_t set_id, int32_t map_id ) const {
            if ( m_songs_path.empty( ) )
                return L"";

            if ( set_id > 0 ) {
                const auto prefix = std::to_wstring( set_id ) + L" ";
                try {
                    for ( const auto& entry : std::filesystem::directory_iterator( m_songs_path ) ) {
                        if ( !entry.is_directory( ) )
                            continue;
                        const auto name = entry.path( ).filename( ).wstring( );
                        if ( name.rfind( prefix, 0 ) != 0 )
                            continue;

                        const auto found = scan_folder_for_map( entry.path( ), map_id );
                        if ( !found.empty( ) )
                            return found;
                    }
                }
                catch ( ... ) {}
            }

            try {
                for ( const auto& entry : std::filesystem::recursive_directory_iterator( m_songs_path ) ) {
                    if ( !entry.is_regular_file( ) || entry.path( ).extension( ) != L".osu" )
                        continue;

                    if ( file_has_beatmap_id( entry.path( ), map_id ) )
                        return entry.path( ).wstring( );
                }
            }
            catch ( ... ) {}

            return L"";
        }

        static std::wstring scan_folder_for_map( const std::filesystem::path& folder, int32_t map_id ) {
            try {
                for ( const auto& entry : std::filesystem::directory_iterator( folder ) ) {
                    if ( !entry.is_regular_file( ) || entry.path( ).extension( ) != L".osu" )
                        continue;
                    if ( file_has_beatmap_id( entry.path( ), map_id ) )
                        return entry.path( ).wstring( );
                }
            }
            catch ( ... ) {}
            return L"";
        }

        static bool file_has_beatmap_id( const std::filesystem::path& path, int32_t map_id ) {
            std::ifstream file( path );
            if ( !file )
                return false;

            std::string line;
            while ( std::getline( file, line ) ) {
                if ( line.find( "BeatmapID:" ) != std::string::npos )
                    return parse_beatmap_id_line( trim_narrow( line ) ) == map_id;
            }
            return false;
        }

        struct timing_point_t {
            int32_t offset = 0;
            float beat_length = 500.f;
            bool uninherited = true;
        };

        static float beat_length_at( const std::vector<timing_point_t>& points, int32_t time ) {
            if ( points.empty( ) )
                return 500.f;

            float last_uninherited = 500.f;
            float current = 500.f;

            for ( const auto& point : points ) {
                if ( point.offset > time )
                    break;

                if ( point.uninherited ) {
                    last_uninherited = point.beat_length;
                    current = point.beat_length;
                }
                else if ( point.beat_length < 0.f ) {
                    current = last_uninherited * ( point.beat_length / -100.f );
                }
                else {
                    current = last_uninherited;
                }
            }

            if ( current <= 0.f || std::isnan( current ) || std::isinf( current ) )
                return 500.f;
            return current;
        }

        static void project_to_screen( osu::hit_object_t& obj, int32_t sw, int32_t sh ) {
            const float playfield_height = sh * 0.8f;
            const float playfield_width = playfield_height * ( 4.f / 3.f );
            const float scale = playfield_width / 512.f;
            const float offset_x = ( sw - playfield_width ) * 0.5f;
            const float offset_y = ( sh - playfield_height ) * 0.5f;

            const float stack_offset = -static_cast<float>( obj.stack_index ) * 6.f * scale;
            obj.screen_x = offset_x + ( obj.x * scale ) + stack_offset;
            obj.screen_y = offset_y + ( obj.y * scale ) + stack_offset + 17.f;
        }

        bool parse_file( const std::wstring& path, osu::beatmap_data_t& out ) {
            std::ifstream file( path );
            if ( !file )
                return false;

            std::string line;
            std::string section;
            float slider_multiplier = 1.4f;
            std::vector<timing_point_t> timing_points;

            int32_t last_x = -1;
            int32_t last_y = -1;
            int32_t last_stack = 0;

            while ( std::getline( file, line ) ) {
                if ( line.empty( ) )
                    continue;
                if ( line.front( ) == '[' ) {
                    section = line;
                    continue;
                }

                if ( section == "[Difficulty]" ) {
                    if ( line.rfind( "CircleSize:", 0 ) == 0 )
                        out.cs = std::stof( line.substr( 11 ) );
                    else if ( line.rfind( "SliderMultiplier:", 0 ) == 0 )
                        slider_multiplier = std::stof( line.substr( 17 ) );
                }
                else if ( section == "[TimingPoints]" ) {
                    const auto vars = split_csv( line );
                    if ( vars.size( ) < 2 )
                        continue;

                    timing_point_t tp{};
                    tp.offset = std::stoi( vars[ 0 ] );
                    tp.beat_length = std::stof( vars[ 1 ] );
                    tp.uninherited = vars.size() >= 7 ? std::stoi( vars[ 6 ] ) != 0 : tp.beat_length >= 0.f;
                    timing_points.push_back( tp );
                }
                else if ( section == "[HitObjects]" ) {
                    const auto vars = split_csv( line );
                    if ( vars.size( ) < 4 )
                        continue;

                    const int32_t x = std::stoi( vars[ 0 ] );
                    const int32_t y = std::stoi( vars[ 1 ] );
                    const int32_t time = std::stoi( vars[ 2 ] );
                    const int32_t type = std::stoi( vars[ 3 ] );

                    osu::hit_object_t obj{};
                    obj.x = static_cast<float>( x );
                    obj.y = static_cast<float>( y );
                    obj.start_time = time;
                    obj.end_time = time;
                    obj.type = static_cast<uint8_t>( type );

                    obj.stack_index = ( last_x == x && last_y == y ) ? last_stack + 1 : 0;
                    last_x = x;
                    last_y = y;
                    last_stack = obj.stack_index;

                    if ( type & static_cast<int>( osu::hit_object_type_t::slider ) ) {
                        if ( vars.size( ) >= 8 ) {
                            int32_t repeat = std::stoi( vars[ 6 ] );
                            if ( repeat < 1 )
                                repeat = 1;
                            const float length = std::stof( vars[ 7 ] );
                            const float beat_length = beat_length_at( timing_points, time );

                            obj.slider_curve_str = vars[ 5 ];
                            obj.slider_length = length;
                            obj.slider_repeat = repeat;

                            if ( slider_multiplier > 0.f && beat_length > 0.f && length > 0.f ) {
                                const float one_lap = ( length / ( slider_multiplier * 100.f ) ) * beat_length;
                                obj.end_time = time + static_cast<int32_t>( std::ceil( one_lap * static_cast<float>( repeat ) ) );
                            }
                        }
                    }
                    else if ( type & static_cast<int>( osu::hit_object_type_t::spinner ) ) {
                        if ( vars.size( ) >= 6 )
                            obj.end_time = std::stoi( vars[ 5 ] );
                    }
                    else {
                        obj.end_time = time + 100;
                    }

                    project_to_screen( obj, out.screen_width, out.screen_height );
                    out.objects.push_back( obj );
                }
            }

            std::sort( out.objects.begin( ), out.objects.end( ),
                []( const osu::hit_object_t& a, const osu::hit_object_t& b ) {
                    return a.start_time < b.start_time;
                } );

            return !out.objects.empty( );
        }
    };

}