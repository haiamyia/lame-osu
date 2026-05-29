#pragma once

#include <impl/struct/game_snapshot.hxx>
#include <impl/memory/input.hxx>
#include <impl/util/playfield.hxx>
#include <Windows.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <sstream>

namespace autobot {

    struct point_t {
        float x = 0.f;
        float y = 0.f;
    };

    class c_autobot {
    public:
        bool enabled = false;

        void on_leave_play( const osu::game_snapshot_t& game ) {
            release_keys( );
            m_click_queue.clear( );
            m_slider_cache.clear( );
            m_scheduled_through_idx = -1;
            m_last_active_idx = -1;
            m_synced = false;
            m_in_play = false;
            m_driving_cursor = false;
            m_use_k2_next = false;
            m_last_click_time = -99999;
            m_spinner_active = false;
            m_spinner_start_time = -1;
        }

        void update( const osu::game_snapshot_t& game, const osu::beatmap_data_t& map, bool user_control = false ) {
            if ( !enabled || game.cur_state != osu::game_state_t::play || !map.loaded || map.objects.empty( ) ) {
                if ( m_in_play )
                    on_leave_play( game );
                return;
            }

            if ( user_control ) {
                if ( m_driving_cursor ) {
                    release_keys( );
                    m_driving_cursor = false;
                }
                return;
            }

            m_driving_cursor = true;

            const HWND hwnd = input::target_window( );
            RECT window{};
            if ( !hwnd || !playfield::get_playfield_rect( hwnd, window ) ) {
                if ( m_in_play ) on_leave_play( game );
                return;
            }

            m_in_play = true;
            const int gt = game.cur_time;
            const int win_w = window.right - window.left;
            const int win_h = window.bottom - window.top;

            WORD k1 = 0, k2 = 0;
            resolve_keys( game, k1, k2 );
            m_k1_actual = k1;
            m_k2_actual = k2;

            if ( !m_synced || gt < m_last_game_time - 200 ) {
                release_keys( );
                m_click_queue.clear( );
                m_slider_cache.clear( );
                m_scheduled_through_idx = -1;
                m_last_active_idx = -1;
                m_use_k2_next = false;
                m_last_click_time = -99999;
                m_spinner_active = false;
                m_spinner_start_time = -1;
                m_synced = true;
            }
            m_last_game_time = gt;

            schedule_clicks( game, map );

            const int spinner_idx = find_spinner_at_time( gt, map );
            if ( spinner_idx >= 0 ) {
                drive_spinner_motion( map.objects[ static_cast<size_t>( spinner_idx ) ], window, win_w, win_h );
            }
            else {
                m_spinner_active = false;
                m_spinner_start_time = -1;

                point_t target_win = get_target_position( gt, map, win_w, win_h );
                const int sx = static_cast<int>( window.left + target_win.x );
                const int sy = static_cast<int>( window.top + target_win.y );
                input::move_absolute_virtual_desktop( sx, sy );
            }

            flush_queue( gt );
        }

    private:
        struct scheduled_click_t {
            int press_time = 0;
            int release_time = 0;
            WORD key = 0;
            bool pressed = false;
            bool released = false;
        };

        struct cached_slider_t {
            int start_time = -1;
            std::vector<point_t> path;
            std::vector<float> dists;
            float total_dist = 0.f;
        };

        std::vector<scheduled_click_t> m_click_queue;
        std::vector<cached_slider_t> m_slider_cache;
        int m_scheduled_through_idx = -1;
        int m_last_active_idx = -1;
        bool m_in_play = false;
        bool m_synced = false;
        int m_last_game_time = 0;
        bool m_k1_down = false;
        bool m_k2_down = false;
        bool m_driving_cursor = false;
        bool m_use_k2_next = false;
        int  m_last_click_time = -99999;
        WORD m_k1_actual = 0;
        WORD m_k2_actual = 0;
        bool m_spinner_active = false;
        int m_spinner_start_time = -1;
        float m_spinner_angle = 0.f;
        uint64_t m_spinner_last_wall_ms = 0;

        static void resolve_keys( const osu::game_snapshot_t& game, WORD& k1, WORD& k2 ) {
            k1 = static_cast<WORD>( game.left_key );
            k2 = static_cast<WORD>( game.right_key );
            if ( !k1 ) k1 = 'Z';
            if ( !k2 ) k2 = 'X';
        }

        void release_keys( ) {
            if ( m_k1_down ) { input::release_vk( m_k1_actual ); m_k1_down = false; }
            if ( m_k2_down ) { input::release_vk( m_k2_actual ); m_k2_down = false; }
        }

        void press_key( WORD vk, bool& down ) {
            if ( !vk || down ) return;
            input::press_vk( vk );
            down = true;
        }

        void release_key( WORD vk, bool& down ) {
            if ( !vk || !down ) return;
            input::release_vk( vk );
            down = false;
        }

        static constexpr float k_spinner_omega_rad_per_ms = 477.f * 6.283185307f / 60000.f;

        static bool is_spinner_object( const osu::hit_object_t& obj ) {
            if ( obj.type & static_cast<uint8_t>( osu::hit_object_type_t::spinner ) )
                return true;

            if ( !( obj.type & static_cast<uint8_t>( osu::hit_object_type_t::slider ) ) )
                return false;
            if ( !obj.slider_curve_str.empty( ) || obj.slider_length > 0.f )
                return false;

            const int dur = obj.end_time - obj.start_time;
            if ( dur < 400 )
                return false;

            const float dx = obj.x - 256.f;
            const float dy = obj.y - 192.f;
            return dx * dx + dy * dy < 48.f * 48.f;
        }

        static point_t spinner_osu_at_angle( const osu::hit_object_t& obj, float angle ) {
            constexpr float k_center_x = 256.f;
            constexpr float k_center_y = 192.f;
            constexpr float k_radius = 90.f;

            const float cx = ( std::abs( obj.x ) > 1.f || std::abs( obj.y ) > 1.f ) ? obj.x : k_center_x;
            const float cy = ( std::abs( obj.x ) > 1.f || std::abs( obj.y ) > 1.f ) ? obj.y : k_center_y;
            return { cx + std::cos( angle ) * k_radius, cy + std::sin( angle ) * k_radius };
        }

        void drive_spinner_motion( const osu::hit_object_t& obj, const RECT& window, int win_w, int win_h ) {
            const uint64_t now = GetTickCount64( );

            if ( !m_spinner_active || m_spinner_start_time != obj.start_time ) {
                m_spinner_active = true;
                m_spinner_start_time = obj.start_time;
                m_spinner_angle = 0.f;
                m_spinner_last_wall_ms = now;

                const point_t raw = spinner_osu_at_angle( obj, 0.f );
                float wx = 0.f, wy = 0.f;
                playfield::project_osu_to_window( raw.x, raw.y, win_w, win_h, wx, wy, 0 );
                input::move_absolute_virtual_desktop(
                    static_cast<int>( window.left + wx ),
                    static_cast<int>( window.top + wy ) );
                return;
            }

            float dt = static_cast<float>( now - m_spinner_last_wall_ms );
            m_spinner_last_wall_ms = now;
            if ( dt <= 0.f )
                return;
            if ( dt > 100.f )
                dt = 16.f;

            const float prev_angle = m_spinner_angle;
            m_spinner_angle += dt * k_spinner_omega_rad_per_ms;

            const point_t p0 = spinner_osu_at_angle( obj, prev_angle );
            const point_t p1 = spinner_osu_at_angle( obj, m_spinner_angle );
            float w0x = 0.f, w0y = 0.f, w1x = 0.f, w1y = 0.f;
            playfield::project_osu_to_window( p0.x, p0.y, win_w, win_h, w0x, w0y, 0 );
            playfield::project_osu_to_window( p1.x, p1.y, win_w, win_h, w1x, w1y, 0 );

            const int dx = static_cast<int>( window.left + w1x ) - static_cast<int>( window.left + w0x );
            const int dy = static_cast<int>( window.top + w1y ) - static_cast<int>( window.top + w0y );
            if ( dx == 0 && dy == 0 )
                return;

            if ( !input::move_screen_delta( dx, dy ) )
                input::move_relative( dx, dy );
        }

        static int find_spinner_at_time( int gt, const osu::beatmap_data_t& map ) {
            int best_idx = -1;
            int best_start = -1;
            for ( size_t i = 0; i < map.objects.size( ); ++i ) {
                const auto& obj = map.objects[ i ];
                if ( !is_spinner_object( obj ) )
                    continue;
                if ( gt < obj.start_time - 8 || gt > obj.end_time + 8 )
                    continue;
                if ( obj.start_time >= best_start ) {
                    best_start = obj.start_time;
                    best_idx = static_cast<int>( i );
                }
            }
            return best_idx;
        }

        static int circle_hold_ms( int press_time, int next_start_time ) {
            int gap = next_start_time - press_time;
            if ( gap <= 0 || gap >= 99999 )
                return 22;

            if ( gap <= 80 )
                return std::max( 5, gap / 2 - 2 );

            int hold = 22;
            if ( hold > gap / 2 )
                hold = gap / 2;
            return std::max( 5, hold );
        }

        void schedule_clicks( const osu::game_snapshot_t& game, const osu::beatmap_data_t& map ) {
            WORD k1 = 0, k2 = 0;
            resolve_keys( game, k1, k2 );

            for ( int i = m_scheduled_through_idx + 1; i < static_cast<int>( map.objects.size( ) ); ++i ) {
                const auto& obj = map.objects[ static_cast<size_t>( i ) ];

                if ( is_spinner_object( obj ) ) {
                    m_use_k2_next = !m_use_k2_next;
                    const WORD chosen = m_use_k2_next ? k2 : k1;
                    if ( chosen ) {
                        int release_time = obj.end_time;
                        if ( release_time <= obj.start_time )
                            release_time = obj.start_time + 1000;
                        m_click_queue.push_back( { obj.start_time, release_time, chosen, false, false } );
                        m_last_click_time = obj.start_time;
                    }
                    m_scheduled_through_idx = i;
                    continue;
                }

                const int press_time = obj.start_time;
                int release_time = obj.end_time;

                if ( !( obj.type & static_cast<uint8_t>( osu::hit_object_type_t::slider ) ) ) {
                    int next_start = press_time + 500;
                    if ( i + 1 < static_cast<int>( map.objects.size( ) ) )
                        next_start = map.objects[ static_cast<size_t>( i + 1 ) ].start_time;

                    const int hold = circle_hold_ms( press_time, next_start );
                    release_time = press_time + hold;
                }

                m_use_k2_next = !m_use_k2_next;
                const WORD chosen = m_use_k2_next ? k2 : k1;
                if ( !chosen )
                    continue;

                m_click_queue.push_back( { press_time, release_time, chosen, false, false } );
                m_last_click_time = press_time;
                m_scheduled_through_idx = i;
            }
        }

        void flush_queue( int gt ) {
            const WORD k1 = m_k1_actual;
            const WORD k2 = m_k2_actual;

            for ( auto& c : m_click_queue ) {
                if ( c.pressed && !c.released && gt >= c.release_time ) {
                    bool& ref = ( c.key == k1 ) ? m_k1_down : m_k2_down;
                    release_key( c.key, ref );
                    c.released = true;
                }
            }

            for ( auto& c : m_click_queue ) {
                if ( c.pressed || c.released )
                    continue;
                if ( gt < c.press_time )
                    continue;

                bool& ref = ( c.key == k1 ) ? m_k1_down : m_k2_down;
                if ( ref )
                    release_key( c.key, ref );

                press_key( c.key, ref );
                c.pressed = true;
            }

            m_click_queue.erase(
                std::remove_if( m_click_queue.begin( ), m_click_queue.end( ),
                    []( const scheduled_click_t& c ) { return c.released; } ),
                m_click_queue.end( ) );
        }

        int get_movement_end_time( const osu::hit_object_t& obj, const osu::beatmap_data_t& map, int idx ) {
            if ( is_spinner_object( obj ) )
                return obj.end_time;
            if ( obj.type & static_cast<uint8_t>( osu::hit_object_type_t::slider ) )
                return obj.end_time;

            int next_start = obj.start_time + 500;
            if ( idx + 1 < static_cast<int>( map.objects.size( ) ) )
                next_start = map.objects[ static_cast<size_t>( idx + 1 ) ].start_time;

            return obj.start_time + circle_hold_ms( obj.start_time, next_start ) + 8;
        }

        point_t get_target_position( int gt, const osu::beatmap_data_t& map, int win_w, int win_h ) {
            if ( map.objects.empty( ) ) {
                float wx = 0.f, wy = 0.f;
                playfield::project_osu_to_window( 256.f, 192.f, win_w, win_h, wx, wy, 0 );
                return { wx, wy };
            }

            if ( gt < map.objects[0].start_time ) {
                const auto& first = map.objects[0];
                float wx = 0.f, wy = 0.f;
                playfield::project_osu_to_window( first.x, first.y, win_w, win_h, wx, wy, first.stack_index );
                return { wx, wy };
            }

            const int spinner_idx = find_spinner_at_time( gt, map );
            if ( spinner_idx >= 0 ) {
                m_last_active_idx = spinner_idx;
                const auto& spin = map.objects[ static_cast<size_t>( spinner_idx ) ];
                point_t raw = spinner_osu_at_angle( spin, m_spinner_angle );
                float wx = 0.f, wy = 0.f;
                playfield::project_osu_to_window( raw.x, raw.y, win_w, win_h, wx, wy, 0 );
                return { wx, wy };
            }

            int active_idx = -1;

            if ( m_last_active_idx >= 0 && m_last_active_idx < static_cast<int>( map.objects.size( ) ) ) {
                const auto& obj = map.objects[ static_cast<size_t>( m_last_active_idx ) ];
                if ( !is_spinner_object( obj ) ) {
                    int end_t = get_movement_end_time( obj, map, m_last_active_idx );
                    if ( gt >= obj.start_time && gt <= end_t ) {
                        active_idx = m_last_active_idx;
                    }
                }
            }

            if ( active_idx < 0 ) {
                int best_start = -1;
                for ( size_t i = 0; i < map.objects.size( ); ++i ) {
                    const auto& obj = map.objects[ i ];
                    if ( is_spinner_object( obj ) )
                        continue;

                    int next_start = obj.start_time + 500;
                    if ( i + 1 < map.objects.size( ) )
                        next_start = map.objects[ i + 1 ].start_time;

                    const int hit_open = obj.start_time - 18;
                    const int hit_close = obj.start_time + circle_hold_ms( obj.start_time, next_start ) + 12;
                    if ( gt >= hit_open && gt <= hit_close && obj.start_time >= best_start ) {
                        active_idx = static_cast<int>( i );
                        best_start = obj.start_time;
                    }
                }
            }

            m_last_active_idx = active_idx;

            if ( active_idx >= 0 ) {
                const auto& obj = map.objects[ static_cast<size_t>( active_idx ) ];
                if ( obj.type & static_cast<uint8_t>( osu::hit_object_type_t::slider ) ) {
                    point_t raw = evaluate_slider( obj, gt );
                    float wx = 0.f, wy = 0.f;
                    playfield::project_osu_to_window( raw.x, raw.y, win_w, win_h, wx, wy, obj.stack_index );
                    return { wx, wy };
                }
                float wx = 0.f, wy = 0.f;
                playfield::project_osu_to_window( obj.x, obj.y, win_w, win_h, wx, wy, obj.stack_index );
                return { wx, wy };
            }

            int prev_idx = -1;
            int next_idx = -1;
            for ( size_t i = 0; i < map.objects.size( ); ++i ) {
                if ( map.objects[i].start_time > gt ) {
                    next_idx = static_cast<int>( i );
                    prev_idx = static_cast<int>( i - 1 );
                    break;
                }
            }

            if ( prev_idx >= 0 && next_idx >= 0 ) {
                const auto& prev_obj = map.objects[ static_cast<size_t>( prev_idx ) ];
                const auto& next_obj = map.objects[ static_cast<size_t>( next_idx ) ];

                int prev_end_t = get_movement_end_time( prev_obj, map, prev_idx );

                point_t start_raw = { prev_obj.x, prev_obj.y };
                if ( prev_obj.type & static_cast<uint8_t>( osu::hit_object_type_t::slider ) ) {
                    start_raw = evaluate_slider( prev_obj, prev_end_t );
                }
                else if ( is_spinner_object( prev_obj ) ) {
                    const float end_angle =
                        static_cast<float>( prev_end_t - prev_obj.start_time ) * k_spinner_omega_rad_per_ms;
                    start_raw = spinner_osu_at_angle( prev_obj, end_angle );
                }

                float start_wx = 0.f, start_wy = 0.f;
                int prev_stack = is_spinner_object( prev_obj ) ? 0 : prev_obj.stack_index;
                playfield::project_osu_to_window( start_raw.x, start_raw.y, win_w, win_h, start_wx, start_wy, prev_stack );

                float end_wx = 0.f, end_wy = 0.f;
                playfield::project_osu_to_window( next_obj.x, next_obj.y, win_w, win_h, end_wx, end_wy, next_obj.stack_index );

                float duration = static_cast<float>( next_obj.start_time - prev_end_t );
                float target_time = duration - 15.f;
                if ( target_time < duration * 0.7f ) {
                    target_time = duration * 0.7f;
                }

                if ( target_time > 0.f ) {
                    float t = static_cast<float>( gt - prev_end_t ) / target_time;
                    t = std::clamp( t, 0.f, 1.f );
                    t = t * t * ( 3.f - 2.f * t );
                    return { start_wx + ( end_wx - start_wx ) * t,
                             start_wy + ( end_wy - start_wy ) * t };
                }
                return { end_wx, end_wy };
            }

            const auto& last_obj = map.objects.back( );
            int last_end_t = get_movement_end_time( last_obj, map, static_cast<int>( map.objects.size( ) - 1 ) );
            point_t last_raw = { last_obj.x, last_obj.y };
            if ( last_obj.type & static_cast<uint8_t>( osu::hit_object_type_t::slider ) ) {
                last_raw = evaluate_slider( last_obj, last_end_t );
            }
            float wx = 0.f, wy = 0.f;
            playfield::project_osu_to_window( last_raw.x, last_raw.y, win_w, win_h, wx, wy, last_obj.stack_index );
            return { wx, wy };
        }

        static point_t evaluate_bezier( const std::vector<point_t>& ctrl, float t ) {
            if ( ctrl.empty( ) ) return { 0.f, 0.f };
            std::vector<point_t> tmp = ctrl;
            size_t n = tmp.size( );
            for ( size_t step = 1; step < n; ++step ) {
                for ( size_t i = 0; i < n - step; ++i ) {
                    tmp[i].x = ( 1.f - t ) * tmp[i].x + t * tmp[i+1].x;
                    tmp[i].y = ( 1.f - t ) * tmp[i].y + t * tmp[i+1].y;
                }
            }
            return tmp[0];
        }

        static std::vector<point_t> generate_bezier_points( const std::vector<point_t>& ctrl ) {
            std::vector<point_t> result;
            if ( ctrl.empty( ) ) return result;

            std::vector<point_t> segment;
            for ( size_t i = 0; i < ctrl.size( ); ++i ) {
                segment.push_back( ctrl[i] );
                if ( i == ctrl.size( ) - 1 || ( std::abs( ctrl[i].x - ctrl[i+1].x ) < 0.01f && std::abs( ctrl[i].y - ctrl[i+1].y ) < 0.01f ) ) {
                    if ( segment.size( ) >= 2 ) {
                        const int num_samples = 32;
                        for ( int s = 0; s <= num_samples; ++s ) {
                            float t = static_cast<float>( s ) / static_cast<float>( num_samples );
                            result.push_back( evaluate_bezier( segment, t ) );
                        }
                    }
                    else if ( !segment.empty( ) ) {
                        result.push_back( segment[0] );
                    }
                    segment.clear( );
                }
            }
            return result;
        }

        static std::vector<point_t> generate_circle_points( const point_t& a, const point_t& b, const point_t& c ) {
            std::vector<point_t> result;
            float d = 2.f * ( a.x * ( b.y - c.y ) + b.x * ( c.y - a.y ) + c.x * ( a.y - b.y ) );
            if ( std::abs( d ) < 0.001f ) {
                result.push_back( a );
                result.push_back( b );
                result.push_back( c );
                return result;
            }

            float ux = ( ( a.x * a.x + a.y * a.y ) * ( b.y - c.y ) + ( b.x * b.x + b.y * b.y ) * ( c.y - a.y ) + ( c.x * c.x + c.y * c.y ) * ( a.y - b.y ) ) / d;
            float uy = ( ( a.x * a.x + a.y * a.y ) * ( c.x - b.x ) + ( b.x * b.x + b.y * b.y ) * ( a.x - c.x ) + ( c.x * c.x + c.y * c.y ) * ( b.x - a.x ) ) / d;
            point_t center = { ux, uy };

            float r = std::sqrt( ( a.x - center.x ) * ( a.x - center.x ) + ( a.y - center.y ) * ( a.y - center.y ) );
            float angle_a = std::atan2( a.y - center.y, a.x - center.x );
            float angle_b = std::atan2( b.y - center.y, b.x - center.x );
            float angle_c = std::atan2( c.y - center.y, c.x - center.x );

            while ( angle_b < angle_a ) angle_b += 2.f * 3.14159265f;
            while ( angle_c < angle_a ) angle_c += 2.f * 3.14159265f;

            if ( angle_b > angle_c ) {
                angle_b -= 2.f * 3.14159265f;
                angle_c -= 2.f * 3.14159265f;
            }

            const int num_samples = 64;
            for ( int s = 0; s <= num_samples; ++s ) {
                float t = static_cast<float>( s ) / static_cast<float>( num_samples );
                float angle = angle_a + t * ( angle_c - angle_a );
                result.push_back( { center.x + std::cos( angle ) * r, center.y + std::sin( angle ) * r } );
            }
            return result;
        }

        std::vector<point_t> generate_slider_path( const osu::hit_object_t& obj ) {
            std::vector<point_t> ctrl;
            ctrl.push_back( { obj.x, obj.y } );

            char type_char = 'L';
            if ( !obj.slider_curve_str.empty( ) ) {
                std::stringstream ss( obj.slider_curve_str );
                std::string item;
                bool is_first = true;
                while ( std::getline( ss, item, '|' ) ) {
                    if ( is_first ) {
                        if ( !item.empty( ) ) type_char = item[0];
                        is_first = false;
                        continue;
                    }
                    auto colon = item.find( ':' );
                    if ( colon != std::string::npos ) {
                        try {
                            float px = std::stof( item.substr( 0, colon ) );
                            float py = std::stof( item.substr( colon + 1 ) );
                            ctrl.push_back( { px, py } );
                        }
                        catch ( ... ) {}
                    }
                }
            }

            if ( ctrl.size( ) < 2 ) {
                return ctrl;
            }

            if ( type_char == 'P' && ctrl.size( ) == 3 ) {
                return generate_circle_points( ctrl[0], ctrl[1], ctrl[2] );
            }
            else if ( type_char == 'B' ) {
                return generate_bezier_points( ctrl );
            }

            return ctrl;
        }

        const cached_slider_t& get_slider_path( const osu::hit_object_t& obj ) {
            for ( const auto& c : m_slider_cache ) {
                if ( c.start_time == obj.start_time )
                    return c;
            }

            cached_slider_t cached;
            cached.start_time = obj.start_time;
            cached.path = generate_slider_path( obj );

            float total_dist = 0.f;
            cached.dists.push_back( 0.f );
            for ( size_t i = 1; i < cached.path.size( ); ++i ) {
                float dx = cached.path[i].x - cached.path[i-1].x;
                float dy = cached.path[i].y - cached.path[i-1].y;
                float d = std::sqrt( dx * dx + dy * dy );
                total_dist += d;
                cached.dists.push_back( total_dist );
            }
            cached.total_dist = total_dist;

            m_slider_cache.push_back( cached );
            return m_slider_cache.back( );
        }

        point_t evaluate_slider( const osu::hit_object_t& obj, int gt ) {
            const auto& cached = get_slider_path( obj );
            if ( cached.path.size( ) < 2 || cached.total_dist <= 0.f ) {
                return { obj.x, obj.y };
            }

            const float duration = static_cast<float>( obj.end_time - obj.start_time );
            if ( duration <= 0.f ) return { obj.x, obj.y };

            const float elapsed = static_cast<float>( gt - obj.start_time );
            float t = std::clamp( elapsed / duration, 0.f, 1.f );

            int repeats = obj.slider_repeat;
            if ( repeats < 1 ) repeats = 1;

            float repeat_prog = t * static_cast<float>( repeats );
            int current_lap = static_cast<int>( repeat_prog );
            float lap_t = repeat_prog - static_cast<float>( current_lap );
            if ( current_lap >= repeats ) {
                lap_t = ( repeats % 2 == 1 ) ? 1.f : 0.f;
            }
            else if ( current_lap % 2 == 1 ) {
                lap_t = 1.f - lap_t;
            }

            float target_dist = lap_t * cached.total_dist;
            point_t pos = cached.path.back( );
            for ( size_t i = 1; i < cached.path.size( ); ++i ) {
                if ( target_dist <= cached.dists[i] ) {
                    float segment_len = cached.dists[i] - cached.dists[i-1];
                    float seg_t = segment_len > 0.f ? ( target_dist - cached.dists[i-1] ) / segment_len : 0.f;
                    pos.x = cached.path[i-1].x + ( cached.path[i].x - cached.path[i-1].x ) * seg_t;
                    pos.y = cached.path[i-1].y + ( cached.path[i].y - cached.path[i-1].y ) * seg_t;
                    break;
                }
            }
            return pos;
        }
    };

}