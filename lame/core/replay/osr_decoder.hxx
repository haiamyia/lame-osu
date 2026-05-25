#pragma once

#include <impl/deps/lzma_decode.hxx>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace replay {

    struct frame_t {
        int32_t delta_ms = 0;
        float x = 0.f;
        float y = 0.f;
        uint8_t keys = 0;
        int32_t absolute_time = 0;
    };

    struct replay_data_t {
        std::string player_name;
        int32_t map_id = 0;
        int16_t mods = 0;
        std::vector<frame_t> frames;
        bool valid = false;
        std::string last_error;
    };

    class c_osr_decoder {
    public:
        [[nodiscard]] const std::string& last_error( ) const { return m_last_error; }

        bool load( const std::wstring& path, replay_data_t& out ) {
            m_last_error.clear( );
            out = {};
            out.last_error.clear( );

            std::ifstream file( path, std::ios::binary );
            if ( !file ) {
                m_last_error = "failed to open replay file";
                out.last_error = m_last_error;
                return false;
            }

            auto read_byte = [ & ]() -> uint8_t {
                uint8_t b = 0;
                file.read( reinterpret_cast<char*>( &b ), 1 );
                return b;
            };

            auto read_i32 = [ & ]() -> int32_t {
                int32_t v = 0;
                file.read( reinterpret_cast<char*>( &v ), 4 );
                return v;
            };

            auto read_u16 = [ & ]() -> uint16_t {
                uint16_t v = 0;
                file.read( reinterpret_cast<char*>( &v ), 2 );
                return v;
            };

            auto read_i64 = [ & ]() -> int64_t {
                int64_t v = 0;
                file.read( reinterpret_cast<char*>( &v ), 8 );
                return v;
            };

            auto read_osu_string = [ & ]() -> std::string {
                if ( read_byte( ) != 0x0b )
                    return {};

                int32_t length = 0;
                int shift = 0;
                while ( shift != 0x23 ) {
                    const uint8_t b = read_byte( );
                    length |= static_cast<int32_t>( b & 0x7f ) << shift;
                    shift += 7;
                    if ( ( b & 0x80 ) == 0 )
                        break;
                }

                if ( length <= 0 )
                    return {};

                std::string s( static_cast<size_t>( length ), '\0' );
                file.read( s.data( ), length );
                return s;
            };

            auto read_byte_array = [ & ]() -> std::vector<uint8_t> {
                const int32_t length = read_i32( );
                if ( length <= 0 )
                    return {};
                if ( length > 50 * 1024 * 1024 ) {
                    m_last_error = "compressed frame data too large";
                    return {};
                }
                std::vector<uint8_t> data( static_cast<size_t>( length ) );
                file.read( reinterpret_cast<char*>( data.data( ) ), length );
                return data;
            };

            read_byte( ); // play mode
            read_i32( );  // version

            read_osu_string( ); // beatmap hash
            out.player_name = read_osu_string( );
            read_osu_string( ); // replay hash

            read_u16( ); // count 300
            read_u16( ); // count 100
            read_u16( ); // count 50
            read_u16( ); // geki
            read_u16( ); // katu
            read_u16( ); // miss
            read_i32( ); // score
            read_u16( ); // max combo
            read_byte( ); // perfect
            out.mods = static_cast<int16_t>( read_i32( ) & 0xFFFF );

            read_osu_string( );
            read_i64( );

            const auto compressed = read_byte_array( );
            if ( compressed.empty( ) ) {
                if ( m_last_error.empty( ) )
                    m_last_error = "empty compressed frame data";
                out.last_error = m_last_error;
                return false;
            }

            std::string decoded;
            if ( !lzma::decode_osr_frames( compressed, decoded ) ) {
                m_last_error = "lzma decode failed";
                out.last_error = m_last_error;
                return false;
            }

            parse_frames( decoded, out );
            out.valid = !out.frames.empty( );
            if ( !out.valid ) {
                m_last_error = "frame parse produced zero frames";
                out.last_error = m_last_error;
            }
            return out.valid;
        }

    private:
        std::string m_last_error;

        static std::vector<std::string> split( const std::string& text, char delim ) {
            std::vector<std::string> parts;
            std::stringstream ss( text );
            std::string item;
            while ( std::getline( ss, item, delim ) ) {
                if ( !item.empty( ) )
                    parts.push_back( item );
            }
            return parts;
        }

        static void parse_frames( const std::string& data, replay_data_t& out ) {
            int32_t last_time = 0;

            for ( const auto& block : split( data, ',' ) ) {
                if ( block.empty( ) )
                    continue;

                const auto tokens = split( block, '|' );
                if ( tokens.size( ) < 4 )
                    continue;

                if ( tokens[ 0 ] == "-12345" )
                    continue;

                try {
                    const int32_t delta = std::stoi( tokens[ 0 ] );
                    const float x = std::stof( tokens[ 1 ] );
                    const float y = std::stof( tokens[ 2 ] );
                    const int keys = std::stoi( tokens[ 3 ] );

                    if ( std::isnan( x ) || std::isnan( y ) || std::isinf( x ) || std::isinf( y ) )
                        continue;

                    frame_t frame{};
                    frame.delta_ms = delta;
                    frame.absolute_time = delta + last_time;
                    frame.x = x;
                    frame.y = y;
                    frame.keys = static_cast<uint8_t>( keys );
                    last_time = frame.absolute_time;
                    out.frames.push_back( frame );
                }
                catch ( ... ) {
                    continue;
                }
            }
        }
    };

}