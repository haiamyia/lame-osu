#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <core/aim_assist/assist_physics.hxx>
#include <core/aim_assist/slider_path.hxx>
#include <impl/struct/game_snapshot.hxx>
#include <impl/memory/input.hxx>
#include <impl/util/playfield.hxx>
#include <Windows.h>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <atomic>
#include <mutex>
#include <optional>

namespace aim_assist {

    class c_aimbot {
    public:
        bool  enabled         = false;
        bool  ignore_sliders  = false;
        bool  legit_mode      = true;
        bool  use_hitbox      = true;
        bool  tablet_mode     = true;

        float strength          = 6.f;
        int   timing_ms         = 100;
        float aim_cone_deg      = 60.f;
        float idle_threshold_px = 2.f;
        float blend_early       = 0.15f;
        float blend_late        = 0.85f;
        float return_rate       = 0.08f;
        float stick_rate        = 0.12f;
        float pre_pull          = 0.f;

        void on_leave_play( ) {
            m_in_play.store( false );
            m_user_blocked.store( false );
            clear_motion_state( );
            std::lock_guard<std::mutex> lock( m_target_mutex );
            m_target = {};
        }

        void set_user_input_blocked( bool blocked ) {
            const bool was = m_user_blocked.exchange( blocked );
            if ( blocked && !was )
                clear_motion_state( );
        }

        void update( const osu::game_snapshot_t& game, const osu::beatmap_data_t& map ) {
            if ( m_user_blocked.load( ) ) {
                m_in_play.store( false );
                return;
            }

            const bool in_play = enabled
                                 && game.cur_state == osu::game_state_t::play
                                 && map.loaded
                                 && !map.objects.empty( );
            m_in_play.store( in_play );

            if ( !in_play ) {
                std::lock_guard<std::mutex> lock( m_target_mutex );
                m_target = {};
                return;
            }

            const HWND hwnd = input::target_window( );
            RECT window{};
            if ( !hwnd || !playfield::get_playfield_rect( hwnd, window ) ) {
                std::lock_guard<std::mutex> lock( m_target_mutex );
                m_target = {};
                return;
            }

            const int win_w = window.right - window.left;
            const int win_h = window.bottom - window.top;

            aim_target_t next{};
            next.cur_time_ms = game.cur_time;
            int32_t best_tl = INT32_MAX;

            for ( const auto& obj : map.objects ) {
                if ( obj.type & static_cast<uint8_t>( osu::hit_object_type_t::spinner ) )
                    continue;
                if ( ignore_sliders && ( obj.type & static_cast<uint8_t>( osu::hit_object_type_t::slider ) ) )
                    continue;
                if ( game.cur_time > obj.end_time )
                    continue;

                const int32_t tl = obj.start_time - game.cur_time;
                if ( next.valid && tl >= best_tl )
                    continue;

                float ox = obj.x;
                float oy = obj.y;
                if ( obj.type & static_cast<uint8_t>( osu::hit_object_type_t::slider ) ) {
                    const osu_point_t p = m_sliders.position_at( obj, game.cur_time );
                    ox = p.x;
                    oy = p.y;
                }

                float wx = 0.f, wy = 0.f;
                playfield::project_osu_to_window( ox, oy, win_w, win_h, wx, wy, obj.stack_index );

                next.x = static_cast<float>( window.left ) + wx;
                next.y = static_cast<float>( window.top ) + wy;
                next.hit_time_ms = obj.start_time;
                next.hit_r = hit_radius_screen( map.cs, win_w, win_h );
                next.valid = true;
                best_tl = tl;
            }

            smooth_target( next );

            {
                std::lock_guard<std::mutex> lock( m_target_mutex );
                m_target = next;
            }

            apply_fallback_tick( );
        }

        std::optional<POINT> apply_hook_move( POINT raw, const MSLLHOOKSTRUCT& ) {
            if ( !enabled || m_user_blocked.load( ) )
                return std::nullopt;

            apply_on_input( static_cast<float>( raw.x ), static_cast<float>( raw.y ), true );
            return std::nullopt;
        }

    private:
        static constexpr float k_hook_offset_cap_px     = 9.f;
        static constexpr float k_fallback_offset_cap_px = 14.f;
        static constexpr float k_target_step_px         = 14.f;
        static constexpr float k_target_snap_px         = 80.f;
        static constexpr uint64_t k_min_hook_apply_ms   = 2;
        static constexpr uint64_t k_fallback_apply_ms   = 8;

        std::atomic<bool>     m_in_play{ false };
        std::atomic<bool>     m_user_blocked{ false };
        std::atomic<uint64_t> m_last_apply_ms{ 0 };
        bool                 m_hand_valid = false;
        float                m_hand_x = 0.f;
        float                m_hand_y = 0.f;
        bool                 m_smooth_target_valid = false;
        float                m_smooth_target_x = 0.f;
        float                m_smooth_target_y = 0.f;
        float                m_apply_remainder_x = 0.f;
        float                m_apply_remainder_y = 0.f;
        std::mutex           m_target_mutex;
        std::mutex           m_apply_mutex;
        aim_target_t         m_target{};
        aim_assist_state_t   m_state{};
        c_slider_paths       m_sliders;

        void clear_motion_state( ) {
            m_hand_valid = false;
            m_smooth_target_valid = false;
            m_apply_remainder_x = 0.f;
            m_apply_remainder_y = 0.f;
            m_state = {};
        }

        aim_assist_config_t build_config( ) const {
            aim_assist_config_t cfg{};
            cfg.enabled = enabled;
            cfg.legit_mode = legit_mode;
            cfg.use_hitbox = use_hitbox;
            cfg.strength = std::clamp( strength / 12.f, 0.f, 1.f );
            cfg.timing_ms = timing_ms;
            cfg.aim_cone_deg = aim_cone_deg;
            cfg.idle_threshold_px = idle_threshold_px;
            cfg.blend_early = blend_early;
            cfg.blend_late = blend_late;
            cfg.return_rate = return_rate;
            cfg.stick_rate = stick_rate;
            cfg.pre_pull = pre_pull;
            return cfg;
        }

        void smooth_target( aim_target_t& target ) {
            if ( !target.valid ) {
                m_smooth_target_valid = false;
                return;
            }

            if ( !m_smooth_target_valid ) {
                m_smooth_target_x = target.x;
                m_smooth_target_y = target.y;
                m_smooth_target_valid = true;
            }
            else {
                float dx = target.x - m_smooth_target_x;
                float dy = target.y - m_smooth_target_y;
                const float dist = std::sqrt( dx * dx + dy * dy );
                if ( dist > k_target_snap_px ) {
                    m_smooth_target_x = target.x;
                    m_smooth_target_y = target.y;
                }
                else {
                    float t = 1.f;
                    if ( dist > k_target_step_px )
                        t = k_target_step_px / dist;
                    m_smooth_target_x += dx * t;
                    m_smooth_target_y += dy * t;
                }
            }

            target.x = m_smooth_target_x;
            target.y = m_smooth_target_y;
        }

        void apply_on_input( float raw_x, float raw_y, bool from_hook ) {
            if ( !enabled || !m_in_play.load( ) )
                return;

            const uint64_t now = GetTickCount64( );
            if ( from_hook ) {
                if ( now - m_last_apply_ms.load( ) < k_min_hook_apply_ms )
                    return;
            }

            m_last_apply_ms.store( now );

            const float cap = from_hook ? k_hook_offset_cap_px : k_fallback_offset_cap_px;
            try_apply_move( raw_x, raw_y, cap, from_hook );
        }

        void apply_fallback_tick( ) {
            if ( !enabled || !m_in_play.load( ) )
                return;

            const uint64_t now = GetTickCount64( );
            if ( now - m_last_apply_ms.load( ) < k_fallback_apply_ms )
                return;

            POINT cur{};
            if ( !input::get_cursor_pos( &cur ) )
                return;

            apply_on_input( static_cast<float>( cur.x ), static_cast<float>( cur.y ), false );
        }

        void try_apply_move( float raw_x, float raw_y, float cap_px, bool fixed_dt ) {
            std::lock_guard<std::mutex> lock( m_apply_mutex );

            if ( !tablet_mode ) {
                m_hand_x = raw_x;
                m_hand_y = raw_y;
                m_hand_valid = true;
            }

            aim_target_t target{};
            {
                std::lock_guard<std::mutex> tlock( m_target_mutex );
                target = m_target;
            }

            if ( fixed_dt )
                m_state.last_tick_ms = GetTickCount64( ) - static_cast<uint64_t>( k_dt_norm_ms );

            const float prev_offset_x = m_state.offset_x;
            const float prev_offset_y = m_state.offset_y;

            float out_x = 0.f;
            float out_y = 0.f;
            if ( !apply( raw_x, raw_y, target, build_config( ), m_state, &out_x, &out_y ) )
                return;

            apply_offset_delta( prev_offset_x, prev_offset_y, cap_px );
        }

        void apply_offset_delta( float prev_offset_x, float prev_offset_y, float cap_px ) {
            float assist_dx = m_state.offset_x - prev_offset_x;
            float assist_dy = m_state.offset_y - prev_offset_y;

            if ( legit_mode && ( assist_dx * assist_dx + assist_dy * assist_dy ) < 0.04f )
                return;

            const float assist_len = std::sqrt( assist_dx * assist_dx + assist_dy * assist_dy );
            if ( assist_len > cap_px && assist_len > 0.001f ) {
                const float s = cap_px / assist_len;
                assist_dx *= s;
                assist_dy *= s;
            }

            m_apply_remainder_x += assist_dx;
            m_apply_remainder_y += assist_dy;

            const int dx = static_cast<int>( std::lround( m_apply_remainder_x ) );
            const int dy = static_cast<int>( std::lround( m_apply_remainder_y ) );
            if ( dx == 0 && dy == 0 )
                return;

            m_apply_remainder_x -= static_cast<float>( dx );
            m_apply_remainder_y -= static_cast<float>( dy );
            input::move_relative( dx, dy );
        }

        static float hit_radius_screen( float cs, int win_w, int win_h ) {
            const float playfield_height = static_cast<float>( win_h ) * 0.8f;
            const float osu_scale = ( playfield_height * ( 4.f / 3.f ) ) / 512.f;
            const float osu_radius = 54.4f - 4.48f * cs;
            return std::max( 8.f, osu_radius * osu_scale );
        }
    };

}