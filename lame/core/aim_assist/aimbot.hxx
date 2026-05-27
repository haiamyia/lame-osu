#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <impl/struct/game_snapshot.hxx>
#include <impl/memory/input.hxx>
#include <impl/util/playfield.hxx>
#include <Windows.h>
#include <cmath>
#include <algorithm>
#include <atomic>
#include <optional>

namespace aim_assist {

    class c_aimbot {
    public:
        bool enabled = false;
        bool ignore_sliders = false;

        float strength = 5.f;
        float dead_zone = 30.f;
        int32_t timing_window = 120;
        float drift_decay = 0.15f;
        float passive_pull = 0.35f;

        void on_leave_play( ) {
            reset_state( );
            clear_hook_snapshot( );
        }

        void update( const osu::game_snapshot_t& game, const osu::beatmap_data_t& map ) {
            if ( !enabled || game.cur_state != osu::game_state_t::play || !map.loaded || map.objects.empty( ) ) {
                reset_state( );
                clear_hook_snapshot( );
                return;
            }

            const HWND hwnd = input::target_window( );
            RECT window{};
            if ( !hwnd || !playfield::get_playfield_rect( hwnd, window ) ) {
                reset_state( );
                clear_hook_snapshot( );
                return;
            }

            const int win_w = window.right - window.left;
            const int win_h = window.bottom - window.top;
            const float osu_pixel_radius = 54.4f - 4.48f * map.cs;
            m_hitbox_radius = osu_pixel_radius * ( static_cast<float>( win_h ) / 480.f );

            target_info_t target{};
            find_target( game, map, window, win_w, win_h, target );
            publish_hook_snapshot( target );
        }

        std::optional<POINT> apply_hook_move( POINT pen, const MSLLHOOKSTRUCT& ) {
            const int idx = m_snap_read_idx.load( std::memory_order_acquire );
            const hook_snapshot_t snap = m_snap[ idx ];

            if ( !snap.active )
                return std::nullopt;

            target_info_t target{};
            if ( snap.has_target ) {
                target.valid = true;
                target.sx = snap.target_sx;
                target.sy = snap.target_sy;
                target.dist_px = snap.target_dist;
                target.is_slider = snap.target_is_slider;
            }

            step_assist(
                static_cast<float>( pen.x ), static_cast<float>( pen.y ), target,
                snap.strength, snap.dead_zone,
                snap.drift_decay, snap.passive_pull, snap.hitbox_radius );

            if ( std::abs( m_offset_x ) < 0.15f && std::abs( m_offset_y ) < 0.15f )
                return std::nullopt;

            POINT out{};
            out.x = pen.x + static_cast<LONG>( m_offset_x );
            out.y = pen.y + static_cast<LONG>( m_offset_y );
            return out;
        }

    private:
        struct target_info_t {
            bool valid = false;
            int sx = 0;
            int sy = 0;
            float dist_px = 0.f;
            size_t obj_index = 0;
            bool is_slider = false;
        };

        struct hook_snapshot_t {
            bool active = false;
            bool has_target = false;
            int target_sx = 0;
            int target_sy = 0;
            float target_dist = 0.f;
            bool target_is_slider = false;
            float hitbox_radius = 0.f;
            float strength = 5.f;
            float dead_zone = 30.f;
            float drift_decay = 0.15f;
            float passive_pull = 0.35f;
        };

        float m_offset_x = 0.f;
        float m_offset_y = 0.f;
        float m_last_phys_x = 0.f;
        float m_last_phys_y = 0.f;
        bool m_phys_init = false;
        float m_hitbox_radius = 0.f;

        float m_smooth_tx = 0.f;
        float m_smooth_ty = 0.f;
        bool m_smooth_target_init = false;

        size_t m_locked_index = SIZE_MAX;
        bool m_locked_valid = false;

        hook_snapshot_t m_snap[ 2 ]{};
        std::atomic<int> m_snap_read_idx{ 0 };

        void reset_state( ) {
            m_offset_x = 0.f;
            m_offset_y = 0.f;
            m_last_phys_x = 0.f;
            m_last_phys_y = 0.f;
            m_phys_init = false;
            m_smooth_target_init = false;
            m_locked_valid = false;
            m_locked_index = SIZE_MAX;
        }

        void clear_hook_snapshot( ) {
            const int wi = 1 - m_snap_read_idx.load( std::memory_order_relaxed );
            m_snap[ wi ] = {};
            m_snap_read_idx.store( wi, std::memory_order_release );
        }

        void publish_hook_snapshot( const target_info_t& target ) {
            const int wi = 1 - m_snap_read_idx.load( std::memory_order_relaxed );
            auto& s = m_snap[ wi ];
            s.active = true;
            s.has_target = target.valid;
            s.target_sx = target.sx;
            s.target_sy = target.sy;
            s.target_dist = target.dist_px;
            s.target_is_slider = target.is_slider;
            s.hitbox_radius = m_hitbox_radius;
            s.strength = strength;
            s.dead_zone = dead_zone;
            s.drift_decay = drift_decay;
            s.passive_pull = passive_pull;
            m_snap_read_idx.store( wi, std::memory_order_release );
        }

        void smooth_target_toward( const target_info_t& target ) {
            if ( !target.valid ) {
                m_smooth_target_init = false;
                return;
            }
            const float tx = static_cast<float>( target.sx );
            const float ty = static_cast<float>( target.sy );
            if ( !m_smooth_target_init ) {
                m_smooth_tx = tx;
                m_smooth_ty = ty;
                m_smooth_target_init = true;
                return;
            }
            const float jx = tx - m_smooth_tx;
            const float jy = ty - m_smooth_ty;
            const float jump = std::sqrt( jx * jx + jy * jy );
            const float blend = jump > 120.f ? 0.10f : ( jump > 50.f ? 0.16f : 0.22f );
            m_smooth_tx += jx * blend;
            m_smooth_ty += jy * blend;
        }

        void find_target(
            const osu::game_snapshot_t& game,
            const osu::beatmap_data_t& map,
            const RECT& window, int win_w, int win_h,
            target_info_t& out ) {

            POINT cursor{};
            if ( !input::get_cursor_pos( &cursor ) )
                return;
            const float px = static_cast<float>( cursor.x ) - m_offset_x;
            const float py = static_cast<float>( cursor.y ) - m_offset_y;

            if ( m_locked_valid && m_locked_index < map.objects.size( ) ) {
                const auto& obj = map.objects[ m_locked_index ];
                const bool alive = game.cur_time <= obj.end_time
                    && ( obj.start_time - game.cur_time ) <= timing_window;
                const bool skip = ( obj.type & static_cast<uint8_t>( osu::hit_object_type_t::spinner ) )
                    || ( ignore_sliders && ( obj.type & static_cast<uint8_t>( osu::hit_object_type_t::slider ) ) );

                if ( alive && !skip ) {
                    float wx = 0.f, wy = 0.f;
                    playfield::project_osu_to_window( obj.x, obj.y, win_w, win_h, wx, wy, obj.stack_index );
                    const float sx = static_cast<float>( window.left ) + wx;
                    const float sy = static_cast<float>( window.top ) + wy;
                    const float dx = sx - px, dy = sy - py;
                    out.valid = true;
                    out.sx = static_cast<int>( sx );
                    out.sy = static_cast<int>( sy );
                    out.dist_px = std::sqrt( dx * dx + dy * dy );
                    out.obj_index = m_locked_index;
                    out.is_slider = ( obj.type & static_cast<uint8_t>( osu::hit_object_type_t::slider ) ) != 0;
                    return;
                }
                m_locked_valid = false;
                m_locked_index = SIZE_MAX;
            }

            float best_score = -1e18f;
            size_t best_index = 0;
            bool found = false;

            for ( size_t i = 0; i < map.objects.size( ); ++i ) {
                const auto& obj = map.objects[ i ];
                if ( obj.type & static_cast<uint8_t>( osu::hit_object_type_t::spinner ) )
                    continue;
                if ( ignore_sliders && ( obj.type & static_cast<uint8_t>( osu::hit_object_type_t::slider ) ) )
                    continue;
                const int32_t tl = obj.start_time - game.cur_time;
                if ( tl > timing_window || game.cur_time > obj.end_time )
                    continue;

                float wx = 0.f, wy = 0.f;
                playfield::project_osu_to_window( obj.x, obj.y, win_w, win_h, wx, wy, obj.stack_index );
                const float sx = static_cast<float>( window.left ) + wx;
                const float sy = static_cast<float>( window.top ) + wy;
                const float dx = sx - px, dy = sy - py;
                const float dist = std::sqrt( dx * dx + dy * dy );
                float score = -dist;

                if ( m_phys_init ) {
                    const float vx = px - m_last_phys_x, vy = py - m_last_phys_y;
                    const float spd = std::sqrt( vx * vx + vy * vy );
                    if ( spd > 0.1f && dist > 0.1f ) {
                        const float dot = ( vx * dx + vy * dy ) / ( spd * dist );
                        if ( dot > 0.f ) score += dot * 48.f;
                    }
                }

                if ( score <= best_score ) continue;
                best_score = score;
                best_index = i;
                out.sx = static_cast<int>( sx );
                out.sy = static_cast<int>( sy );
                out.dist_px = dist;
                out.obj_index = i;
                out.is_slider = ( obj.type & static_cast<uint8_t>( osu::hit_object_type_t::slider ) ) != 0;
                found = true;
            }

            if ( found ) {
                m_locked_valid = true;
                m_locked_index = best_index;
            }
            out.valid = found;
        }

        void step_assist(
            float phys_x, float phys_y, const target_info_t& target,
            float str = -1.f, float dz = -1.f,
            float decay = -1.f, float pp = -1.f, float hitbox = -1.f ) {

            if ( str < 0.f ) str = strength;
            if ( dz < 0.f ) dz = dead_zone;
            if ( decay < 0.f ) decay = drift_decay;
            if ( pp < 0.f ) pp = passive_pull;
            if ( hitbox < 0.f ) hitbox = m_hitbox_radius;

            str = std::clamp( str, 0.f, 20.f );
            dz = std::clamp( dz, 0.f, 100.f );
            decay = std::clamp( decay, 0.01f, 0.50f );
            pp = std::clamp( pp, 0.f, 1.f );

            float vel_x = 0.f, vel_y = 0.f;
            if ( m_phys_init ) {
                vel_x = phys_x - m_last_phys_x;
                vel_y = phys_y - m_last_phys_y;
                if ( vel_x * vel_x + vel_y * vel_y > 35.f * 35.f ) {
                    m_offset_x *= 0.5f;
                    m_offset_y *= 0.5f;
                    m_smooth_target_init = false;
                }
            }
            m_last_phys_x = phys_x;
            m_last_phys_y = phys_y;
            m_phys_init = true;

            const float speed = std::sqrt( vel_x * vel_x + vel_y * vel_y );
            const float cx = phys_x + m_offset_x;
            const float cy = phys_y + m_offset_y;

            smooth_target_toward( target );

            if ( target.valid && m_smooth_target_init ) {
                const float tdx = m_smooth_tx - cx;
                const float tdy = m_smooth_ty - cy;
                const float tdist = std::sqrt( tdx * tdx + tdy * tdy );
                const float dead_r = hitbox * std::clamp( dz / 100.f, 0.f, 1.f );
                if ( tdist <= dead_r )
                    return;
            }

            float desired_x = 0.f, desired_y = 0.f;

            if ( target.valid && m_smooth_target_init ) {
                const float dx = m_smooth_tx - cx;
                const float dy = m_smooth_ty - cy;
                const float dist = std::sqrt( dx * dx + dy * dy );

                if ( dist > 0.5f ) {
                    const float dead_r = hitbox * std::clamp( dz / 100.f, 0.f, 1.f );
                    const float dir_x = dx / dist;
                    const float dir_y = dy / dist;

                    const float dead_fade = dead_r > 0.f
                        ? std::clamp( ( dist - dead_r ) / ( dead_r * 0.5f + 3.f ), 0.f, 1.f )
                        : 1.f;
                    const float micro_fade = std::clamp( dist / 3.f, 0.f, 1.f );
                    const float boundary = dead_fade * micro_fade;

                    if ( boundary > 0.001f ) {
                        float precision = 1.f;
                        if ( dist < hitbox * 2.f && hitbox > 0.f ) {
                            const float c = 1.f - dist / ( hitbox * 2.f );
                            precision = 1.f + c * c * 2.0f;
                        }

                        const float base = str * 0.04f;
                        float motion = 0.f;

                        if ( speed >= 0.3f ) {
                            const float dot = vel_x * dir_x + vel_y * dir_y;
                            const float approach = std::clamp( dot / speed, 0.f, 1.f );
                            if ( approach > 0.001f ) {
                                const float sb = std::clamp( speed / 5.f, 0.6f, 1.5f );
                                motion = approach * ( 1.f + approach * 0.5f ) * sb;
                            }
                        }
                        else {
                            motion = target.is_slider ? 0.f : pp;
                        }

                        desired_x = dx * base * motion * precision * boundary;
                        desired_y = dy * base * motion * precision * boundary;

                        const float dmag = std::sqrt( desired_x * desired_x + desired_y * desired_y );
                        const float cap = dist * 0.65f;
                        if ( dmag > cap && dmag > 0.01f ) {
                            desired_x *= cap / dmag;
                            desired_y *= cap / dmag;
                        }
                    }
                }
            }

            float ddx = ( desired_x - m_offset_x ) * decay;
            float ddy = ( desired_y - m_offset_y ) * decay;

            const float max_d = 1.5f + str * 0.15f;
            const float dm = std::sqrt( ddx * ddx + ddy * ddy );
            if ( dm > max_d && dm > 0.01f ) {
                ddx *= max_d / dm;
                ddy *= max_d / dm;
            }

            m_offset_x += ddx;
            m_offset_y += ddy;

            if ( std::abs( m_offset_x ) < 0.05f ) m_offset_x = 0.f;
            if ( std::abs( m_offset_y ) < 0.05f ) m_offset_y = 0.f;
        }
    };

}