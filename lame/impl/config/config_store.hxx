#pragma once

#include <Windows.h>
#include <ShlObj.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <cctype>
#include <algorithm>

namespace config {

    struct settings_t {
        bool aim_enabled = false;
        bool aim_ignore_sliders = false;
        bool aim_legit_mode = true;
        bool aim_use_hitbox = true;
        bool aim_tablet_mode = false;
        float aim_strength = 6.f;
        int aim_timing_ms = 100;
        float aim_cone_deg = 60.f;
        float aim_idle_threshold_px = 2.f;
        float aim_blend_early = 0.15f;
        float aim_blend_late = 0.85f;
        float aim_return_rate = 0.08f;
        float aim_stick_rate = 0.12f;
        float aim_pre_pull = 0.f;

        bool relax_enabled = false;
        float relax_hit_window_ms = 10.f;
        int relax_tap_style = 0;
        int relax_singletap_bpm_cap = 100;
        float relax_k1_hold_center = 68.f;
        float relax_k1_hold_spread = 18.f;
        float relax_k2_hold_center = 83.f;
        float relax_k2_hold_spread = 16.f;
        float relax_hold_floor = 30.f;
        float relax_hold_ceiling = 115.f;
        int relax_manual_offset_ms = 0;

        bool replay_enabled = false;
        std::string replay_path_utf8;
        float replay_speed_multiplier = 1.f;
        int replay_time_offset_ms = 0;
        bool replay_flip = false;
        bool replay_disable_aim = false;
        bool replay_disable_clicking = false;
        int replay_y_offset = 17;

        bool autobot_enabled = false;

        int custom_left_key = 'Z';
        int custom_right_key = 'X';
        bool stream_proof = false;
        std::string songs_path_utf8;
    };

    inline std::filesystem::path configs_dir( ) {
        wchar_t appdata[ MAX_PATH ]{};
        if ( SUCCEEDED( SHGetFolderPathW( nullptr, CSIDL_APPDATA, nullptr, 0, appdata ) ) ) {
            std::filesystem::path dir = std::filesystem::path( appdata ) / L"lame" / L"configs";
            std::error_code ec;
            std::filesystem::create_directories( dir, ec );
            return dir;
        }
        std::filesystem::path dir = std::filesystem::current_path( ) / "configs";
        std::error_code ec;
        std::filesystem::create_directories( dir, ec );
        return dir;
    }

    inline std::string sanitize_name( std::string name ) {
        name.erase( std::remove_if( name.begin( ), name.end( ),
            []( char c ) {
                return c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' ||
                       c == '<' || c == '>' || c == '|' || c < 32;
            } ),
            name.end( ) );
        while ( !name.empty( ) && std::isspace( static_cast<unsigned char>( name.back( ) ) ) )
            name.pop_back( );
        size_t start = 0;
        while ( start < name.size( ) && std::isspace( static_cast<unsigned char>( name[ start ] ) ) )
            ++start;
        return name.substr( start );
    }

    inline std::filesystem::path profile_path( const std::string& name ) {
        return configs_dir( ) / ( sanitize_name( name ) + ".cfg" );
    }

    inline void write_line( std::ostream& out, const char* key, bool v ) {
        out << key << '=' << ( v ? '1' : '0' ) << '\n';
    }

    inline void write_line( std::ostream& out, const char* key, int v ) {
        out << key << '=' << v << '\n';
    }

    inline void write_line( std::ostream& out, const char* key, float v ) {
        out << key << '=' << v << '\n';
    }

    inline void write_line( std::ostream& out, const char* key, const std::string& v ) {
        out << key << '=';
        for ( char c : v ) {
            if ( c == '\n' || c == '\r' )
                continue;
            if ( c == '\\' )
                out << "\\\\";
            else if ( c == '=' )
                out << "\\e";
            else
                out << c;
        }
        out << '\n';
    }

    inline std::string unescape_value( std::string v ) {
        std::string out;
        out.reserve( v.size( ) );
        for ( size_t i = 0; i < v.size( ); ++i ) {
            if ( v[ i ] == '\\' && i + 1 < v.size( ) ) {
                if ( v[ i + 1 ] == '\\' ) {
                    out.push_back( '\\' );
                    ++i;
                }
                else if ( v[ i + 1 ] == 'e' ) {
                    out.push_back( '=' );
                    ++i;
                }
                else {
                    out.push_back( v[ i ] );
                }
            }
            else {
                out.push_back( v[ i ] );
            }
        }
        return out;
    }

    inline bool save_profile( const std::string& name, const settings_t& s ) {
        const auto path = profile_path( name );
        if ( path.empty( ) )
            return false;

        std::ofstream out( path, std::ios::trunc );
        if ( !out )
            return false;

        write_line( out, "profile_name", name );
        write_line( out, "aim.enabled", s.aim_enabled );
        write_line( out, "aim.ignore_sliders", s.aim_ignore_sliders );
        write_line( out, "aim.legit_mode", s.aim_legit_mode );
        write_line( out, "aim.use_hitbox", s.aim_use_hitbox );
        write_line( out, "aim.tablet_mode", s.aim_tablet_mode );
        write_line( out, "aim.strength", s.aim_strength );
        write_line( out, "aim.timing_ms", s.aim_timing_ms );
        write_line( out, "aim.cone_deg", s.aim_cone_deg );
        write_line( out, "aim.idle_threshold_px", s.aim_idle_threshold_px );
        write_line( out, "aim.blend_early", s.aim_blend_early );
        write_line( out, "aim.blend_late", s.aim_blend_late );
        write_line( out, "aim.return_rate", s.aim_return_rate );
        write_line( out, "aim.stick_rate", s.aim_stick_rate );
        write_line( out, "aim.pre_pull", s.aim_pre_pull );

        write_line( out, "relax.enabled", s.relax_enabled );
        write_line( out, "relax.hit_window_ms", s.relax_hit_window_ms );
        write_line( out, "relax.tap_style", s.relax_tap_style );
        write_line( out, "relax.singletap_bpm_cap", s.relax_singletap_bpm_cap );
        write_line( out, "relax.k1_hold_center", s.relax_k1_hold_center );
        write_line( out, "relax.k1_hold_spread", s.relax_k1_hold_spread );
        write_line( out, "relax.k2_hold_center", s.relax_k2_hold_center );
        write_line( out, "relax.k2_hold_spread", s.relax_k2_hold_spread );
        write_line( out, "relax.hold_floor", s.relax_hold_floor );
        write_line( out, "relax.hold_ceiling", s.relax_hold_ceiling );
        write_line( out, "relax.manual_offset_ms", s.relax_manual_offset_ms );

        write_line( out, "replay.enabled", s.replay_enabled );
        write_line( out, "replay.path", s.replay_path_utf8 );
        write_line( out, "replay.speed_multiplier", s.replay_speed_multiplier );
        write_line( out, "replay.time_offset_ms", s.replay_time_offset_ms );
        write_line( out, "replay.flip", s.replay_flip );
        write_line( out, "replay.disable_aim", s.replay_disable_aim );
        write_line( out, "replay.disable_clicking", s.replay_disable_clicking );
        write_line( out, "replay.y_offset", s.replay_y_offset );

        write_line( out, "autobot.enabled", s.autobot_enabled );

        write_line( out, "keys.left", s.custom_left_key );
        write_line( out, "keys.right", s.custom_right_key );
        write_line( out, "system.stream_proof", s.stream_proof );
        write_line( out, "system.songs_path", s.songs_path_utf8 );
        return true;
    }

    inline bool parse_bool( const std::string& v, bool& out ) {
        if ( v == "1" || v == "true" || v == "True" || v == "yes" ) {
            out = true;
            return true;
        }
        if ( v == "0" || v == "false" || v == "False" || v == "no" ) {
            out = false;
            return true;
        }
        return false;
    }

    inline bool load_profile( const std::string& name, settings_t& s ) {
        const auto path = profile_path( name );
        std::ifstream in( path );
        if ( !in )
            return false;

        s = settings_t{};
        std::string line;
        while ( std::getline( in, line ) ) {
            if ( line.empty( ) || line[ 0 ] == '#' )
                continue;
            const auto eq = line.find( '=' );
            if ( eq == std::string::npos )
                continue;

            const std::string key = line.substr( 0, eq );
            const std::string val = unescape_value( line.substr( eq + 1 ) );

            auto parse_int = [ & ]( int& dst ) {
                try {
                    dst = std::stoi( val );
                }
                catch ( ... ) {
                }
            };
            auto parse_float = [ & ]( float& dst ) {
                try {
                    dst = std::stof( val );
                }
                catch ( ... ) {
                }
            };

            if ( key == "aim.enabled" )
                parse_bool( val, s.aim_enabled );
            else if ( key == "aim.ignore_sliders" )
                parse_bool( val, s.aim_ignore_sliders );
            else if ( key == "aim.legit_mode" )
                parse_bool( val, s.aim_legit_mode );
            else if ( key == "aim.use_hitbox" )
                parse_bool( val, s.aim_use_hitbox );
            else if ( key == "aim.tablet_mode" )
                parse_bool( val, s.aim_tablet_mode );
            else if ( key == "aim.strength" )
                parse_float( s.aim_strength );
            else if ( key == "aim.timing_ms" )
                parse_int( s.aim_timing_ms );
            else if ( key == "aim.cone_deg" )
                parse_float( s.aim_cone_deg );
            else if ( key == "aim.idle_threshold_px" )
                parse_float( s.aim_idle_threshold_px );
            else if ( key == "aim.blend_early" )
                parse_float( s.aim_blend_early );
            else if ( key == "aim.blend_late" )
                parse_float( s.aim_blend_late );
            else if ( key == "aim.return_rate" )
                parse_float( s.aim_return_rate );
            else if ( key == "aim.stick_rate" )
                parse_float( s.aim_stick_rate );
            else if ( key == "aim.pre_pull" )
                parse_float( s.aim_pre_pull );
            else if ( key == "relax.enabled" )
                parse_bool( val, s.relax_enabled );
            else if ( key == "relax.hit_window_ms" )
                parse_float( s.relax_hit_window_ms );
            else if ( key == "relax.tap_style" )
                parse_int( s.relax_tap_style );
            else if ( key == "relax.singletap_bpm_cap" )
                parse_int( s.relax_singletap_bpm_cap );
            else if ( key == "relax.k1_hold_center" )
                parse_float( s.relax_k1_hold_center );
            else if ( key == "relax.k1_hold_spread" )
                parse_float( s.relax_k1_hold_spread );
            else if ( key == "relax.k2_hold_center" )
                parse_float( s.relax_k2_hold_center );
            else if ( key == "relax.k2_hold_spread" )
                parse_float( s.relax_k2_hold_spread );
            else if ( key == "relax.hold_floor" )
                parse_float( s.relax_hold_floor );
            else if ( key == "relax.hold_ceiling" )
                parse_float( s.relax_hold_ceiling );
            else if ( key == "relax.manual_offset_ms" )
                parse_int( s.relax_manual_offset_ms );
            else if ( key == "replay.enabled" )
                parse_bool( val, s.replay_enabled );
            else if ( key == "replay.path" )
                s.replay_path_utf8 = val;
            else if ( key == "replay.speed_multiplier" )
                parse_float( s.replay_speed_multiplier );
            else if ( key == "replay.time_offset_ms" )
                parse_int( s.replay_time_offset_ms );
            else if ( key == "replay.flip" )
                parse_bool( val, s.replay_flip );
            else if ( key == "replay.disable_aim" )
                parse_bool( val, s.replay_disable_aim );
            else if ( key == "replay.disable_clicking" )
                parse_bool( val, s.replay_disable_clicking );
            else if ( key == "replay.y_offset" )
                parse_int( s.replay_y_offset );
            else if ( key == "autobot.enabled" )
                parse_bool( val, s.autobot_enabled );
            else if ( key == "keys.left" )
                parse_int( s.custom_left_key );
            else if ( key == "keys.right" )
                parse_int( s.custom_right_key );
            else if ( key == "system.stream_proof" )
                parse_bool( val, s.stream_proof );
            else if ( key == "system.songs_path" )
                s.songs_path_utf8 = val;
        }
        return true;
    }

    inline std::vector<std::string> list_profiles( ) {
        std::vector<std::string> names;
        std::error_code ec;
        for ( const auto& entry : std::filesystem::directory_iterator( configs_dir( ), ec ) ) {
            if ( !entry.is_regular_file( ) )
                continue;
            if ( entry.path( ).extension( ) != ".cfg" )
                continue;
            names.push_back( entry.path( ).stem( ).string( ) );
        }
        std::sort( names.begin( ), names.end( ) );
        return names;
    }

}