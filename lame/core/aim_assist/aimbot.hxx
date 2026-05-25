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
        bool require_motion = true;
        bool ignore_sliders = false;
        bool tablet_mode = true;

        float pull_strength = 5.f;
        float return_close = 0.15f;
        float return_far = 0.2f;
        int32_t timing_window = 120;
        float min_hand_speed = 0.3f;

        float max_aim_angle = 45.f;
        float blend_angle = 20.f;
        float motion_curve = 0.7f;
        float hold_radius = 0.92f;

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

            if ( tablet_mode ) {
                publish_hook_snapshot( target );
                return;
            }

            clear_hook_snapshot( );

            POINT cursor{};
            if ( !input::get_cursor_pos( &cursor ) )
                return;

            const float phys_x = static_cast<float>( cursor.x ) - m_offset_x;
            const float phys_y = static_cast<float>( cursor.y ) - m_offset_y;

            step_assist( phys_x, phys_y, target );

            const int out_x = static_cast<int>( phys_x + m_offset_x );
            const int out_y = static_cast<int>( phys_y + m_offset_y );
            input::move_absolute_virtual_desktop( out_x, out_y );
        }

        std::optional<POINT> apply_hook_move( POINT pen, const MSLLHOOKSTRUCT& /*raw*/ ) {
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
            }

            const float phys_x = static_cast<float>( pen.x );
            const float phys_y = static_cast<float>( pen.y );

            step_assist(
                phys_x, phys_y, target, snap.return_close, snap.return_far, snap.pull_strength, snap.hitbox_radius,
                snap.max_aim_angle, snap.blend_angle, snap.motion_curve, snap.require_motion, snap.hold_radius );

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
        };

        struct hook_snapshot_t {
            bool active = false;
            bool has_target = false;
            bool require_motion = true;
            int target_sx = 0;
            int target_sy = 0;
            float target_dist = 0.f;
            float hitbox_radius = 0.f;
            float return_close = 0.15f;
            float return_far = 0.25f;
            float pull_strength = 6.f;
            float max_aim_angle = 45.f;
            float blend_angle = 20.f;
            float motion_curve = 0.7f;
            float hold_radius = 0.92f;
        };

        float m_offset_x = 0.f;
        float m_offset_y = 0.f;
        float m_want_x = 0.f;
        float m_want_y = 0.f;
        float m_last_phys_x = 0.f;
        float m_last_phys_y = 0.f;
        bool m_phys_init = false;
        float m_hitbox_radius = 0.f;

        float m_smooth_tx = 0.f;
        float m_smooth_ty = 0.f;
        bool m_smooth_target_init = false;
        bool m_on_note = false;

        size_t m_locked_index = SIZE_MAX;
        bool m_locked_valid = false;

        hook_snapshot_t m_snap[ 2 ]{};
        std::atomic<int> m_snap_read_idx{ 0 };

        void reset_state( ) {
            m_offset_x = 0.f;
            m_offset_y = 0.f;
            m_want_x = 0.f;
            m_want_y = 0.f;
            m_last_phys_x = 0.f;
            m_last_phys_y = 0.f;
            m_phys_init = false;
            m_smooth_target_init = false;
            m_on_note = false;
            m_locked_valid = false;
            m_locked_index = SIZE_MAX;
        }

        static void nudge_toward( float& value, float goal, float rate, float max_step ) {
            float delta = ( goal - value ) * rate;
            if ( delta > max_step )
                delta = max_step;
            else if ( delta < -max_step )
                delta = -max_step;
            value += delta;
        }

        void clear_hook_snapshot( ) {
            const int write_idx = 1 - m_snap_read_idx.load( std::memory_order_relaxed );
            m_snap[ write_idx ] = {};
            m_snap_read_idx.store( write_idx, std::memory_order_release );
        }

        void publish_hook_snapshot( const target_info_t& target ) {
            const int write_idx = 1 - m_snap_read_idx.load( std::memory_order_relaxed );
            m_snap[ write_idx ].active = true;
            m_snap[ write_idx ].has_target = target.valid;
            m_snap[ write_idx ].require_motion = require_motion;
            m_snap[ write_idx ].target_sx = target.sx;
            m_snap[ write_idx ].target_sy = target.sy;
            m_snap[ write_idx ].target_dist = target.dist_px;
            m_snap[ write_idx ].hitbox_radius = m_hitbox_radius;
            m_snap[ write_idx ].return_close = return_close;
            m_snap[ write_idx ].return_far = return_far;
            m_snap[ write_idx ].pull_strength = pull_strength;
            m_snap[ write_idx ].max_aim_angle = max_aim_angle;
            m_snap[ write_idx ].blend_angle = blend_angle;
            m_snap[ write_idx ].motion_curve = motion_curve;
            m_snap[ write_idx ].hold_radius = hold_radius;
            m_snap_read_idx.store( write_idx, std::memory_order_release );
        }

        static float cursor_dist_to_target( float cursor_x, float cursor_y, const target_info_t& target ) {
            const float dx = static_cast<float>( target.sx ) - cursor_x;
            const float dy = static_cast<float>( target.sy ) - cursor_y;
            return std::sqrt( dx * dx + dy * dy );
        }

        static bool cursor_inside_hitobject(
            float cursor_x,
            float cursor_y,
            const target_info_t& target,
            float hitbox,
            float stop_scale,
            float scale_mul = 1.f ) {

            if ( !target.valid || hitbox <= 0.f )
                return false;

            const float stop_r = hitbox * std::clamp( stop_scale, 0.5f, 1.f ) * scale_mul;
            return cursor_dist_to_target( cursor_x, cursor_y, target ) <= stop_r;
        }

        void update_on_note_state(
            float cursor_x,
            float cursor_y,
            const target_info_t& target,
            float hitbox,
            float stop_scale ) {

            if ( !target.valid ) {
                m_on_note = false;
                return;
            }

            if ( !m_on_note ) {
                m_on_note = cursor_inside_hitobject( cursor_x, cursor_y, target, hitbox, stop_scale, 1.f );
                return;
            }

            if ( !cursor_inside_hitobject( cursor_x, cursor_y, target, hitbox, stop_scale, 1.12f ) )
                m_on_note = false;
        }

        void settle_on_hitobject( ) {
            nudge_toward( m_want_x, 0.f, 0.35f, 2.5f );
            nudge_toward( m_want_y, 0.f, 0.35f, 2.5f );
            nudge_toward( m_offset_x, 0.f, 0.28f, 2.0f );
            nudge_toward( m_offset_y, 0.f, 0.28f, 2.0f );
            if ( std::abs( m_offset_x ) < 0.08f )
                m_offset_x = 0.f;
            if ( std::abs( m_offset_y ) < 0.08f )
                m_offset_y = 0.f;
            if ( std::abs( m_want_x ) < 0.08f )
                m_want_x = 0.f;
            if ( std::abs( m_want_y ) < 0.08f )
                m_want_y = 0.f;
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

            const float jump = std::sqrt(
                ( tx - m_smooth_tx ) * ( tx - m_smooth_tx ) + ( ty - m_smooth_ty ) * ( ty - m_smooth_ty ) );
            const float blend = jump > 90.f ? 0.10f : ( jump > 40.f ? 0.14f : 0.20f );
            nudge_toward( m_smooth_tx, tx, blend, 12.f );
            nudge_toward( m_smooth_ty, ty, blend, 12.f );
        }

        static float smoothstep( float edge0, float edge1, float x ) {
            if ( edge0 == edge1 )
                return x < edge0 ? 0.f : 1.f;
            const float t = std::clamp( ( x - edge0 ) / ( edge1 - edge0 ), 0.f, 1.f );
            return t * t * ( 3.f - 2.f * t );
        }

        static float calculate_angle_deg( float vx1, float vy1, float vx2, float vy2 ) {
            const float dot = vx1 * vx2 + vy1 * vy2;
            const float mag1 = std::sqrt( vx1 * vx1 + vy1 * vy1 );
            const float mag2 = std::sqrt( vx2 * vx2 + vy2 * vy2 );
            if ( mag1 < 0.1f || mag2 < 0.1f )
                return 0.f;

            float x = dot / ( mag1 * mag2 );
            x = std::clamp( x, -1.f, 1.f );

            const float negate = static_cast<float>( x < 0 );
            x = std::abs( x );
            float ret = -0.0187293f;
            ret = ret * x + 0.0742610f;
            ret = ret * x - 0.2121144f;
            ret = ret * x + 1.5707288f;
            ret = ret * std::sqrt( 1.f - x );
            ret = ret - 2.f * negate * ret + negate * 3.14159265358979f;
            return ret * 57.295776f;
        }

        static float angle_alignment( float angle_deg, float soft_deg, float hard_deg ) {
            if ( angle_deg >= hard_deg )
                return 0.f;
            if ( angle_deg <= soft_deg )
                return 1.f;
            const float t = ( angle_deg - soft_deg ) / ( hard_deg - soft_deg );
            return 1.f - smoothstep( 0.f, 1.f, t );
        }

        static float curve_alignment( float align, float amount ) {
            const float clamped = std::clamp( align, 0.f, 1.f );
            const float curved = smoothstep( 0.f, 1.f, clamped );
            return clamped + ( curved - clamped ) * std::clamp( amount, 0.f, 1.f );
        }

        static float micro_wobble( float phys_x, float phys_y, float align, float amount ) {
            const float phase = phys_x * 0.0113f + phys_y * 0.0147f;
            return std::sin( phase ) * 0.35f * ( 1.f - align ) * amount;
        }

        static float interpolated_return( float dist_px, float hitbox, float return_close_val, float return_far_val ) {
            const float near_r = std::max( hitbox * 1.5f, 12.f );
            const float far_r = std::max( hitbox * 7.f, near_r + 24.f );
            const float t = smoothstep( near_r, far_r, dist_px );
            return return_close_val + ( return_far_val - return_close_val ) * t;
        }

        static float pull_blend( float pull_strength_val ) {
            if ( pull_strength_val <= 9.f )
                return std::clamp( pull_strength_val / 9.f, 0.f, 1.f );
            return 1.f + ( pull_strength_val - 9.f ) * 0.12f;
        }

        void find_target(
            const osu::game_snapshot_t& game,
            const osu::beatmap_data_t& map,
            const RECT& window,
            int win_w,
            int win_h,
            target_info_t& out ) {

            POINT cursor{};
            if ( !input::get_cursor_pos( &cursor ) )
                return;

            const float phys_x = static_cast<float>( cursor.x ) - m_offset_x;
            const float phys_y = static_cast<float>( cursor.y ) - m_offset_y;

            float best_score = -1e18f;
            size_t best_index = 0;
            bool found = false;

            for ( size_t i = 0; i < map.objects.size( ); ++i ) {
                const auto& obj = map.objects[ i ];
                if ( ignore_sliders &&
                     ( obj.type & static_cast<uint8_t>( osu::hit_object_type_t::slider ) ) )
                    continue;

                const int32_t time_left = obj.start_time - game.cur_time;
                if ( time_left > timing_window || game.cur_time > obj.end_time )
                    continue;

                float target_wx = 0.f;
                float target_wy = 0.f;
                playfield::project_osu_to_window(
                    obj.x, obj.y, win_w, win_h, target_wx, target_wy, obj.stack_index );

                const float sx = static_cast<float>( window.left ) + target_wx;
                const float sy = static_cast<float>( window.top ) + target_wy;
                const float dx = sx - phys_x;
                const float dy = sy - phys_y;
                const float dist = std::sqrt( dx * dx + dy * dy );

                float score = -dist;
                if ( m_phys_init ) {
                    const float vel_x = phys_x - m_last_phys_x;
                    const float vel_y = phys_y - m_last_phys_y;
                    const float speed = std::sqrt( vel_x * vel_x + vel_y * vel_y );
                    if ( speed > 0.1f ) {
                        const float angle = calculate_angle_deg( vel_x, vel_y, dx, dy );
                        const float align = angle_alignment( angle, blend_angle, max_aim_angle );
                        score += align * 48.f;
                    }
                }

                if ( m_locked_valid && i == m_locked_index )
                    score += 32.f;

                if ( score <= best_score )
                    continue;

                best_score = score;
                best_index = i;
                out.sx = static_cast<int>( sx );
                out.sy = static_cast<int>( sy );
                out.dist_px = dist;
                out.obj_index = i;
                found = true;
            }

            if ( found ) {
                m_locked_valid = true;
                m_locked_index = best_index;
            }
            else {
                m_locked_valid = false;
                m_locked_index = SIZE_MAX;
            }

            out.valid = found;
        }

        void step_assist(
            float phys_x,
            float phys_y,
            const target_info_t& target,
            float return_close_val = -1.f,
            float return_far_val = -1.f,
            float pull_strength_val = -1.f,
            float hitbox = -1.f,
            float max_aim_angle_val = -1.f,
            float blend_angle_val = -1.f,
            float motion_curve_val = -1.f,
            bool require_motion_flag = true,
            float hold_radius_val = -1.f ) {

            if ( return_close_val < 0.f )
                return_close_val = return_close;
            if ( return_far_val < 0.f )
                return_far_val = return_far;
            if ( pull_strength_val < 0.f )
                pull_strength_val = pull_strength;
            if ( hitbox < 0.f )
                hitbox = m_hitbox_radius;
            if ( max_aim_angle_val < 0.f )
                max_aim_angle_val = max_aim_angle;
            if ( blend_angle_val < 0.f )
                blend_angle_val = blend_angle;
            if ( motion_curve_val < 0.f )
                motion_curve_val = motion_curve;
            if ( hold_radius_val < 0.f )
                hold_radius_val = hold_radius;

            return_close_val = std::clamp( return_close_val, 0.01f, 0.30f );
            return_far_val = std::clamp( return_far_val, 0.01f, 0.30f );
            max_aim_angle_val = std::clamp( max_aim_angle_val, 5.f, 90.f );
            blend_angle_val = std::clamp( blend_angle_val, 0.f, max_aim_angle_val - 1.f );
            motion_curve_val = std::clamp( motion_curve_val, 0.f, 1.f );
            hold_radius_val = std::clamp( hold_radius_val, 0.5f, 1.f );

            float vel_x = 0.f;
            float vel_y = 0.f;
            if ( m_phys_init ) {
                vel_x = phys_x - m_last_phys_x;
                vel_y = phys_y - m_last_phys_y;
                const float jump_sq = vel_x * vel_x + vel_y * vel_y;
                if ( jump_sq > 35.f * 35.f ) {
                    m_offset_x *= 0.7f;
                    m_offset_y *= 0.7f;
                    m_want_x *= 0.7f;
                    m_want_y *= 0.7f;
                    m_smooth_target_init = false;
                    m_on_note = false;
                }
            }
            m_last_phys_x = phys_x;
            m_last_phys_y = phys_y;
            m_phys_init = true;

            const float user_speed = std::sqrt( vel_x * vel_x + vel_y * vel_y );
            const float cursor_x = phys_x + m_offset_x;
            const float cursor_y = phys_y + m_offset_y;

            update_on_note_state( cursor_x, cursor_y, target, hitbox, hold_radius_val );
            if ( m_on_note ) {
                settle_on_hitobject( );
                return;
            }

            const float dist_for_return = target.valid ? target.dist_px : hitbox * 10.f;
            const float hand_return = interpolated_return( dist_for_return, hitbox, return_close_val, return_far_val );
            const float return_mul = 1.f - hand_return;

            nudge_toward( m_offset_x, 0.f, return_mul, 3.5f );
            nudge_toward( m_offset_y, 0.f, return_mul, 3.5f );
            nudge_toward( m_want_x, 0.f, return_mul * 0.85f, 3.0f );
            nudge_toward( m_want_y, 0.f, return_mul * 0.85f, 3.0f );

            if ( std::abs( m_offset_x ) < 0.02f )
                m_offset_x = 0.f;
            if ( std::abs( m_offset_y ) < 0.02f )
                m_offset_y = 0.f;

            if ( require_motion_flag && user_speed < min_hand_speed )
                return;

            if ( !target.valid )
                return;

            smooth_target_toward( target );

            const float aim_x = m_smooth_tx;
            const float aim_y = m_smooth_ty;

            float dx = aim_x - phys_x;
            float dy = aim_y - phys_y;
            const float distance = std::sqrt( dx * dx + dy * dy );
            if ( distance < 0.5f )
                return;

            float align = 1.f;
            if ( user_speed >= 0.1f ) {
                const float angle = calculate_angle_deg( vel_x, vel_y, dx, dy );
                align = angle_alignment( angle, blend_angle_val, max_aim_angle_val );
                if ( require_motion_flag && align <= 0.001f )
                    return;
                align = curve_alignment( align, motion_curve_val );
            }
            else if ( require_motion_flag ) {
                return;
            }
            else {
                align = 0.35f;
            }

            const float dist_scale = smoothstep( hitbox * 2.5f, hitbox * 0.6f, distance );
            align *= 0.55f + dist_scale * 0.45f;

            float blend = pull_blend( pull_strength_val ) * align;

            if ( user_speed >= 0.1f ) {
                const float vel_dot = ( vel_x * dx + vel_y * dy ) / ( user_speed * distance );
                const float vel_boost = 0.72f + 0.28f * std::clamp( vel_dot, 0.f, 1.f );
                blend *= vel_boost;
            }

            const float pull_rate = std::min( ( 0.10f + blend * 0.22f ) * ( 0.45f + align * 0.55f ), 0.26f );
            const float want_smooth = std::min( 0.14f + align * 0.16f, 0.24f );
            const float max_step = 2.8f + user_speed * 0.22f;

            float want_x = dx * blend;
            float want_y = dy * blend;

            constexpr float y_scale = 0.70f;
            want_y *= y_scale;

            const float dir_x = dx / distance;
            const float dir_y = dy / distance;
            const float wobble = micro_wobble( phys_x, phys_y, align, motion_curve_val * 0.65f );
            want_x += ( -dir_y ) * wobble;
            want_y += dir_x * wobble * y_scale;

            const float max_offset = distance * ( 0.26f + blend * 0.48f );
            const float want_mag = std::sqrt( want_x * want_x + want_y * want_y );
            if ( want_mag > max_offset && want_mag > 0.01f ) {
                want_x = ( want_x / want_mag ) * max_offset;
                want_y = ( want_y / want_mag ) * max_offset;
            }

            nudge_toward( m_want_x, want_x, want_smooth, max_step );
            nudge_toward( m_want_y, want_y, want_smooth, max_step );
            nudge_toward( m_offset_x, m_want_x, pull_rate, max_step * 0.85f );
            nudge_toward( m_offset_y, m_want_y, pull_rate, max_step * 0.85f );
        }
    };

}