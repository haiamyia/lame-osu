#pragma once

#include <impl/struct/game_snapshot.hxx>
#include <impl/memory/input.hxx>
#include <Windows.h>
#include <vector>
#include <algorithm>
#include <random>
#include <cmath>

namespace relax {

    enum class tap_style_t : int {
        alternate = 0,
        singletap = 1
    };

    class c_relax {
    public:
        bool enabled = false;

        float hit_window_ms = 13.f;   // 0-40

        int tap_style = static_cast<int>( tap_style_t::alternate );

        int singletap_bpm_cap = 100;

        float k1_hold_center = 68.f;
        float k1_hold_spread = 18.f;

        float k2_hold_center = 83.f;
        float k2_hold_spread = 16.f;

        float hold_floor = 30.f;
        float hold_ceiling = 115.f;

        int32_t manual_offset_ms = 0;

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
            m_use_k2_next = false;
        }

        void update( const osu::game_snapshot_t& game, const osu::beatmap_data_t& map ) {
            if ( !enabled || game.cur_state != osu::game_state_t::play || !map.loaded || map.objects.empty( ) ) {
                if ( m_in_play )
                    on_leave_play( game );
                return;
            }

            m_in_play = true;
            const int game_time = game.cur_time;

            if ( !m_synced || game_time < m_last_game_time - 200 ) {
                reset_state( game );
                m_synced = true;
                schedule_clicks( game, map );
            }
            m_last_game_time = game_time;

            advance_past_objects( game, map );
            purge_stale( game_time );
            flush_queue( game );
        }

    private:
        struct scheduled_click_t {
            int press_time = 0;
            int release_time = 0;
            WORD key = 0;
            bool pressed = false;
            bool released = false;
        };

        std::vector<scheduled_click_t> m_click_queue;
        bool m_synced = false;
        bool m_in_play = false;
        int m_last_game_time = 0;
        int m_last_hit_obj_idx = -1;
        int m_scheduled_through_idx = -1;
        int m_last_click_time = -99999;
        bool m_use_k2_next = false;
        bool m_left_down = false;
        bool m_right_down = false;

        std::mt19937 m_rng{ std::random_device{}( ) };
        std::normal_distribution<float> m_norm{ 0.f, 1.f };
        std::uniform_real_distribution<float> m_unit{ 0.f, 1.f };

        float generate_hold( float center, float spread ) {
            const float z = m_norm( m_rng );

            const float skew = 0.3f;
            const float shaped = z + skew * ( z * z - 1.f );

            float hold = center + shaped * spread;

            hold += ( m_unit( m_rng ) - 0.5f ) * 2.f;

            return std::clamp( hold, hold_floor, hold_ceiling );
        }

        float hit_jitter( ) {
            return ( m_unit( m_rng ) * 2.f - 1.f ) * hit_window_ms;
        }

        static float bpm_from_interval( int interval_ms ) {
            return interval_ms > 0 ? 60000.f / static_cast<float>( interval_ms ) : 9999.f;
        }

        static void resolve_keys( const osu::game_snapshot_t& game, WORD& k1, WORD& k2 ) {
            k1 = static_cast<WORD>( game.left_key );
            k2 = static_cast<WORD>( game.right_key );
            if ( !k1 ) k1 = 'Z';
            if ( !k2 ) k2 = 'X';
        }

        void reset_state( const osu::game_snapshot_t& game ) {
            release_all_keys( game );
            m_click_queue.clear( );
            m_last_hit_obj_idx = -1;
            m_scheduled_through_idx = -1;
            m_last_click_time = -99999;
            m_use_k2_next = false;
        }

        void release_all_keys( const osu::game_snapshot_t& game ) {
            WORD k1 = 0, k2 = 0;
            resolve_keys( game, k1, k2 );
            if ( m_left_down ) { input::release_vk( k1 ); m_left_down = false; }
            if ( m_right_down ) { input::release_vk( k2 ); m_right_down = false; }
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

        bool should_alternate( int inter_tap_ms ) {
            if ( static_cast<tap_style_t>( tap_style ) == tap_style_t::alternate )
                return true;

            return bpm_from_interval( inter_tap_ms ) >= static_cast<float>( singletap_bpm_cap );
        }

        void advance_past_objects( const osu::game_snapshot_t& game, const osu::beatmap_data_t& map ) {
            const int gt = game.cur_time;
            while ( m_last_hit_obj_idx + 1 < static_cast<int>( map.objects.size( ) ) ) {
                const auto& obj = map.objects[ static_cast<size_t>( m_last_hit_obj_idx + 1 ) ];
                if ( obj.start_time < gt - 50 )
                    m_last_hit_obj_idx++;
                else
                    break;
            }
        }

        void purge_stale( int game_time ) {
            m_click_queue.erase(
                std::remove_if( m_click_queue.begin( ), m_click_queue.end( ),
                    [ game_time ]( const scheduled_click_t& c ) {
                        return !c.pressed && c.press_time < game_time - 50;
                    } ),
                m_click_queue.end( ) );
        }

        void schedule_clicks( const osu::game_snapshot_t& game, const osu::beatmap_data_t& map ) {
            WORD k1 = 0, k2 = 0;
            resolve_keys( game, k1, k2 );

            for ( int i = m_scheduled_through_idx + 1; i < static_cast<int>( map.objects.size( ) ); ++i ) {
                const auto& obj = map.objects[ static_cast<size_t>( i ) ];
                const int press_time = obj.start_time + static_cast<int>( hit_jitter( ) ) + manual_offset_ms;

                const int natural_hold = obj.end_time - obj.start_time;
                int hold_dur = 0;

                if ( natural_hold > 0 ) {
                    const float tail = ( m_unit( m_rng ) * 2.f - 1.f ) * 5.f;
                    hold_dur = natural_hold + static_cast<int>( tail );
                    if ( hold_dur < 15 ) hold_dur = 15;
                }
                else {
                    if ( m_use_k2_next )
                        hold_dur = static_cast<int>( generate_hold( k2_hold_center, k2_hold_spread ) );
                    else
                        hold_dur = static_cast<int>( generate_hold( k1_hold_center, k1_hold_spread ) );
                }

                const int release_time = press_time + hold_dur;

                const int inter_tap = press_time - m_last_click_time;
                if ( should_alternate( inter_tap ) )
                    m_use_k2_next = !m_use_k2_next;

                const WORD chosen = m_use_k2_next ? k2 : k1;
                if ( !chosen ) continue;

                m_click_queue.push_back( { press_time, release_time, chosen, false, false } );
                m_last_click_time = press_time;
                m_scheduled_through_idx = i;
            }
        }

        void flush_queue( const osu::game_snapshot_t& game ) {
            const int gt = game.cur_time;
            WORD k1 = 0, k2 = 0;
            resolve_keys( game, k1, k2 );

            for ( auto& c : m_click_queue ) {
                if ( !c.pressed && gt >= c.press_time ) {
                    bool& ref = ( c.key == k1 ) ? m_left_down : m_right_down;
                    press_key( c.key, ref );
                    c.pressed = true;
                }
                if ( c.pressed && !c.released && gt >= c.release_time ) {
                    bool& ref = ( c.key == k1 ) ? m_left_down : m_right_down;
                    release_key( c.key, ref );
                    c.released = true;
                }
            }

            m_click_queue.erase(
                std::remove_if( m_click_queue.begin( ), m_click_queue.end( ),
                    []( const scheduled_click_t& c ) { return c.released; } ),
                m_click_queue.end( ) );
        }
    };

}