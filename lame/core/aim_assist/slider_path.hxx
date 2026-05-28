#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <impl/struct/osu_types.hxx>
#include <vector>
#include <string>
#include <sstream>
#include <cmath>
#include <algorithm>

namespace aim_assist {

    struct osu_point_t {
        float x = 0.f;
        float y = 0.f;
    };

    class c_slider_paths {
    public:
        void clear( ) { m_cache.clear( ); }

        osu_point_t position_at( const osu::hit_object_t& obj, int32_t game_time ) {
            const auto& cached = get_path( obj );
            if ( cached.path.size( ) < 2 || cached.total_dist <= 0.f )
                return { obj.x, obj.y };

            const float duration = static_cast<float>( obj.end_time - obj.start_time );
            if ( duration <= 0.f )
                return { obj.x, obj.y };

            const float elapsed = static_cast<float>( game_time - obj.start_time );
            float t = std::clamp( elapsed / duration, 0.f, 1.f );

            int repeats = obj.slider_repeat;
            if ( repeats < 1 )
                repeats = 1;

            float repeat_prog = t * static_cast<float>( repeats );
            int current_lap = static_cast<int>( repeat_prog );
            float lap_t = repeat_prog - static_cast<float>( current_lap );
            if ( current_lap >= repeats ) {
                lap_t = ( repeats % 2 == 1 ) ? 1.f : 0.f;
            }
            else if ( current_lap % 2 == 1 ) {
                lap_t = 1.f - lap_t;
            }

            const float target_dist = lap_t * cached.total_dist;
            osu_point_t pos = cached.path.back( );
            for ( size_t i = 1; i < cached.path.size( ); ++i ) {
                if ( target_dist <= cached.dists[ i ] ) {
                    const float segment_len = cached.dists[ i ] - cached.dists[ i - 1 ];
                    const float seg_t = segment_len > 0.f
                        ? ( target_dist - cached.dists[ i - 1 ] ) / segment_len
                        : 0.f;
                    pos.x = cached.path[ i - 1 ].x +
                            ( cached.path[ i ].x - cached.path[ i - 1 ].x ) * seg_t;
                    pos.y = cached.path[ i - 1 ].y +
                            ( cached.path[ i ].y - cached.path[ i - 1 ].y ) * seg_t;
                    break;
                }
            }
            return pos;
        }

    private:
        struct cached_slider_t {
            int32_t start_time = 0;
            std::vector<osu_point_t> path;
            std::vector<float> dists;
            float total_dist = 0.f;
        };

        static osu_point_t evaluate_bezier( const std::vector<osu_point_t>& ctrl, float t ) {
            if ( ctrl.empty( ) )
                return { 0.f, 0.f };
            std::vector<osu_point_t> tmp = ctrl;
            const size_t n = tmp.size( );
            for ( size_t step = 1; step < n; ++step ) {
                for ( size_t i = 0; i < n - step; ++i ) {
                    tmp[ i ].x = ( 1.f - t ) * tmp[ i ].x + t * tmp[ i + 1 ].x;
                    tmp[ i ].y = ( 1.f - t ) * tmp[ i ].y + t * tmp[ i + 1 ].y;
                }
            }
            return tmp[ 0 ];
        }

        static std::vector<osu_point_t> generate_bezier_points( const std::vector<osu_point_t>& ctrl ) {
            std::vector<osu_point_t> result;
            if ( ctrl.empty( ) )
                return result;

            std::vector<osu_point_t> segment;
            for ( size_t i = 0; i < ctrl.size( ); ++i ) {
                segment.push_back( ctrl[ i ] );
                if ( i == ctrl.size( ) - 1 ||
                     ( std::abs( ctrl[ i ].x - ctrl[ i + 1 ].x ) < 0.01f &&
                       std::abs( ctrl[ i ].y - ctrl[ i + 1 ].y ) < 0.01f ) ) {
                    if ( segment.size( ) >= 2 ) {
                        constexpr int num_samples = 32;
                        for ( int s = 0; s <= num_samples; ++s ) {
                            const float t = static_cast<float>( s ) / static_cast<float>( num_samples );
                            result.push_back( evaluate_bezier( segment, t ) );
                        }
                    }
                    else if ( !segment.empty( ) ) {
                        result.push_back( segment[ 0 ] );
                    }
                    segment.clear( );
                }
            }
            return result;
        }

        static std::vector<osu_point_t> generate_circle_points(
            const osu_point_t& a, const osu_point_t& b, const osu_point_t& c ) {
            std::vector<osu_point_t> result;
            const float d = 2.f * ( a.x * ( b.y - c.y ) + b.x * ( c.y - a.y ) + c.x * ( a.y - b.y ) );
            if ( std::abs( d ) < 0.001f ) {
                result.push_back( a );
                result.push_back( b );
                result.push_back( c );
                return result;
            }

            const float ux = ( ( a.x * a.x + a.y * a.y ) * ( b.y - c.y ) +
                               ( b.x * b.x + b.y * b.y ) * ( c.y - a.y ) +
                               ( c.x * c.x + c.y * c.y ) * ( a.y - b.y ) ) /
                             d;
            const float uy = ( ( a.x * a.x + a.y * a.y ) * ( c.x - b.x ) +
                               ( b.x * b.x + b.y * b.y ) * ( a.x - c.x ) +
                               ( c.x * c.x + c.y * c.y ) * ( b.x - a.x ) ) /
                             d;
            const osu_point_t center{ ux, uy };

            const float r = std::sqrt(
                ( a.x - center.x ) * ( a.x - center.x ) + ( a.y - center.y ) * ( a.y - center.y ) );
            float angle_a = std::atan2( a.y - center.y, a.x - center.x );
            float angle_b = std::atan2( b.y - center.y, b.x - center.x );
            float angle_c = std::atan2( c.y - center.y, c.x - center.x );

            while ( angle_b < angle_a )
                angle_b += 2.f * 3.14159265f;
            while ( angle_c < angle_a )
                angle_c += 2.f * 3.14159265f;

            if ( angle_b > angle_c ) {
                angle_b -= 2.f * 3.14159265f;
                angle_c -= 2.f * 3.14159265f;
            }

            constexpr int num_samples = 64;
            for ( int s = 0; s <= num_samples; ++s ) {
                const float t = static_cast<float>( s ) / static_cast<float>( num_samples );
                const float angle = angle_a + t * ( angle_c - angle_a );
                result.push_back(
                    { center.x + std::cos( angle ) * r, center.y + std::sin( angle ) * r } );
            }
            return result;
        }

        static std::vector<osu_point_t> generate_path( const osu::hit_object_t& obj ) {
            std::vector<osu_point_t> ctrl;
            ctrl.push_back( { obj.x, obj.y } );

            char type_char = 'L';
            if ( !obj.slider_curve_str.empty( ) ) {
                std::stringstream ss( obj.slider_curve_str );
                std::string item;
                bool is_first = true;
                while ( std::getline( ss, item, '|' ) ) {
                    if ( is_first ) {
                        if ( !item.empty( ) )
                            type_char = item[ 0 ];
                        is_first = false;
                        continue;
                    }
                    const auto colon = item.find( ':' );
                    if ( colon != std::string::npos ) {
                        try {
                            const float px = std::stof( item.substr( 0, colon ) );
                            const float py = std::stof( item.substr( colon + 1 ) );
                            ctrl.push_back( { px, py } );
                        }
                        catch ( ... ) {
                        }
                    }
                }
            }

            if ( ctrl.size( ) < 2 )
                return ctrl;

            if ( type_char == 'P' && ctrl.size( ) == 3 )
                return generate_circle_points( ctrl[ 0 ], ctrl[ 1 ], ctrl[ 2 ] );
            if ( type_char == 'B' )
                return generate_bezier_points( ctrl );

            return ctrl;
        }

        const cached_slider_t& get_path( const osu::hit_object_t& obj ) {
            for ( const auto& c : m_cache ) {
                if ( c.start_time == obj.start_time )
                    return c;
            }

            cached_slider_t cached;
            cached.start_time = obj.start_time;
            cached.path = generate_path( obj );
            cached.dists.push_back( 0.f );
            for ( size_t i = 1; i < cached.path.size( ); ++i ) {
                const float dx = cached.path[ i ].x - cached.path[ i - 1 ].x;
                const float dy = cached.path[ i ].y - cached.path[ i - 1 ].y;
                const float dist = std::sqrt( dx * dx + dy * dy );
                cached.total_dist += dist;
                cached.dists.push_back( cached.total_dist );
            }

            m_cache.push_back( cached );
            return m_cache.back( );
        }

        std::vector<cached_slider_t> m_cache;
    };

}