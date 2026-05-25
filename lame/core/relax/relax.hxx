#pragma once

#include <impl/struct/game_snapshot.hxx>
#include <impl/memory/input.hxx>
#include <Windows.h>
#include <vector>
#include <algorithm>
#include <random>

namespace relax {

    class c_relax {
    public:
        bool enabled = false;
        float timing_mean_ms = 0.f;
        float timing_stddev_ms = 6.3f;
        float hold_mean_ms = 27.f;
        float hold_stddev_ms = 3.f;
        int32_t manual_offset_ms = 0;
        float replay_speed = 1.f;

        int alt_hard_thresh_ms = 100;
        int alt_soft_thresh_ms = 135;
        float alt_rate_mid_zone = 0.83f;
        float alt_rate_slow_zone = 0.28f;
        float k2_start_bias = 0.63f;

        [[nodiscard]] size_t queue_size( ) const { return m_click_queue.size( ); }
        [[nodiscard]] bool is_synced( ) const { return m_synced; }
        [[nodiscard]] int last_hit_obj_idx( ) const { return m_last_hit_obj_idx; }
        [[nodiscard]] bool is_active( ) const {
            return enabled && m_in_play && m_synced && !m_click_queue.empty( );
        }

        void on_leave_play( const osu::game_snapshot_t& game ) {
            release_all_keys( game );
            m_click_queue.clear( );
            m_last_hit_obj_idx = -1;
            m_scheduled_through_idx = -1;
            m_last_click_time = -99999;
            m_synced = false;
            m_in_play = false;
            m_empty_queue_logged = false;
            m_play_start_time = -1;
        }

        void update( const osu::game_snapshot_t& game, const osu::beatmap_data_t& map ) {
            if ( !enabled || game.cur_state != osu::game_state_t::play || !map.loaded || map.objects.empty( ) ) {
                if ( m_in_play )
                    on_leave_play( game );
                return;
            }

            m_in_play = true;
            const int game_time = game.cur_time;

            if ( m_play_start_time < 0 )
                m_play_start_time = game_time;

            if ( !m_synced || game_time < m_last_game_time - 200 ) {
                reset_state( game );
                m_synced = true;
                m_next_is_right = m_dist_alt( m_rng ) < k2_start_bias;
                m_empty_queue_logged = false;
                m_play_start_time = game_time;
                schedule_all_remaining( game, map );
            }
            m_last_game_time = game_time;

            advance_past_objects( game, map );
            purge_stale_clicks( game_time );
            flush_click_queue( game );

            if ( m_synced && m_click_queue.empty( ) && !m_empty_queue_logged ) {
                const int elapsed = game_time - m_play_start_time;
                if ( elapsed >= 1000 ) {
                    m_empty_queue_logged = true;
                }
            }
        }

    private:
        struct scheduled_click_t {
            int press_time = 0;
            int release_time = 0;
            WORD physical_key = 0;
            bool pressed = false;
            bool released = false;
        };

        std::vector<scheduled_click_t> m_click_queue;
        bool m_synced = false;
        bool m_in_play = false;
        bool m_empty_queue_logged = false;
        int m_play_start_time = -1;
        int m_last_game_time = 0;
        int m_last_hit_obj_idx = -1;
        int m_scheduled_through_idx = -1;
        int m_last_click_time = -99999;
        bool m_next_is_right = false;
        bool m_left_down = false;
        bool m_right_down = false;

        std::mt19937 m_rng{ std::random_device{}( ) };
        std::normal_distribution<float> m_dist_timing{ 0.f, 1.f };
        std::normal_distribution<float> m_dist_hold{ 0.f, 1.f };
        std::uniform_real_distribution<float> m_dist_alt{ 0.f, 1.f };

        static void resolve_keys( const osu::game_snapshot_t& game, WORD& left_key, WORD& right_key ) {
            left_key = static_cast<WORD>( game.left_key );
            right_key = static_cast<WORD>( game.right_key );
            if ( !left_key )
                left_key = 'Z';
            if ( !right_key )
                right_key = 'X';
        }

        void reset_state( const osu::game_snapshot_t& game ) {
            release_all_keys( game );
            m_click_queue.clear( );
            m_last_hit_obj_idx = -1;
            m_scheduled_through_idx = -1;
            m_last_click_time = -99999;
        }

        bool should_alternate( int inter_tap_ms ) {
            if ( inter_tap_ms <= 0 )
                return true;
            if ( inter_tap_ms < alt_hard_thresh_ms )
                return true;
            const float roll = m_dist_alt( m_rng );
            if ( inter_tap_ms < alt_soft_thresh_ms )
                return roll < alt_rate_mid_zone;
            return roll < alt_rate_slow_zone;
        }

        void release_all_keys( const osu::game_snapshot_t& game ) {
            WORD left = 0;
            WORD right = 0;
            resolve_keys( game, left, right );
            if ( m_left_down ) {
                input::release_vk( left );
                m_left_down = false;
            }
            if ( m_right_down ) {
                input::release_vk( right );
                m_right_down = false;
            }
        }

        void press_vk( WORD vk, bool& down_flag ) {
            if ( !vk || down_flag )
                return;
            input::press_vk( vk );
            down_flag = true;
        }

        void release_vk( WORD vk, bool& down_flag ) {
            if ( !vk || !down_flag )
                return;
            input::release_vk( vk );
            down_flag = false;
        }

        void advance_past_objects( const osu::game_snapshot_t& game, const osu::beatmap_data_t& map ) {
            const int game_time = game.cur_time;

            while ( m_last_hit_obj_idx + 1 < static_cast<int>( map.objects.size( ) ) ) {
                const int next_idx = m_last_hit_obj_idx + 1;
                const auto& obj = map.objects[ static_cast<size_t>( next_idx ) ];
                const int ho_time =
                    static_cast<int>( static_cast<double>( obj.start_time ) / replay_speed );

                if ( ho_time < game_time - 50 )
                    m_last_hit_obj_idx = next_idx;
                else
                    break;
            }
        }

        void purge_stale_clicks( int game_time ) {
            m_click_queue.erase(
                std::remove_if( m_click_queue.begin( ), m_click_queue.end( ),
                    [ game_time ]( const scheduled_click_t& sc ) {
                        return !sc.pressed && sc.press_time < game_time - 50;
                    } ),
                m_click_queue.end( ) );
        }

        void schedule_all_remaining( const osu::game_snapshot_t& game, const osu::beatmap_data_t& map ) {
            WORD left_key = 0;
            WORD right_key = 0;
            resolve_keys( game, left_key, right_key );

            for ( int i = m_scheduled_through_idx + 1; i < static_cast<int>( map.objects.size( ) ); ++i ) {
                const auto& obj = map.objects[ static_cast<size_t>( i ) ];
                const int ho_time =
                    static_cast<int>( static_cast<double>( obj.start_time ) / replay_speed );

                const float jitter = m_dist_timing( m_rng ) * timing_stddev_ms + timing_mean_ms;
                const int press_time = ho_time + static_cast<int>( jitter ) + manual_offset_ms;

                int natural_hold = static_cast<int>(
                    static_cast<double>( obj.end_time - obj.start_time ) / replay_speed );

                int hold_dur = 0;
                if ( natural_hold > 0 ) {
                    const float tail_jitter = m_dist_hold( m_rng ) * ( hold_stddev_ms * 0.4f );
                    hold_dur = natural_hold + static_cast<int>( tail_jitter );
                }
                else {
                    hold_dur = static_cast<int>( m_dist_hold( m_rng ) * hold_stddev_ms + hold_mean_ms );
                    if ( hold_dur < 15 )
                        hold_dur = 15;
                }

                const int release_time = press_time + hold_dur;
                const int inter_tap = press_time - m_last_click_time;
                if ( should_alternate( inter_tap ) )
                    m_next_is_right = !m_next_is_right;
                const WORD chosen = m_next_is_right ? right_key : left_key;

                if ( !chosen )
                    continue;

                m_click_queue.push_back( { press_time, release_time, chosen, false, false } );
                m_last_click_time = press_time;
                m_scheduled_through_idx = i;
            }
        }

        void flush_click_queue( const osu::game_snapshot_t& game ) {
            const int game_time = game.cur_time;
            WORD left_key = 0;
            WORD right_key = 0;
            resolve_keys( game, left_key, right_key );

            for ( auto& sc : m_click_queue ) {
                if ( !sc.pressed && game_time >= sc.press_time ) {
                    bool& ref = ( sc.physical_key == left_key ) ? m_left_down : m_right_down;
                    press_vk( sc.physical_key, ref );
                    sc.pressed = true;
                }
                if ( sc.pressed && !sc.released && game_time >= sc.release_time ) {
                    bool& ref = ( sc.physical_key == left_key ) ? m_left_down : m_right_down;
                    release_vk( sc.physical_key, ref );
                    sc.released = true;
                }
            }

            m_click_queue.erase(
                std::remove_if( m_click_queue.begin( ), m_click_queue.end( ),
                    []( const scheduled_click_t& sc ) { return sc.released; } ),
                m_click_queue.end( ) );
        }
    };

}