#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace aim_assist {

    static constexpr float k_dt_norm_ms = 7.518797f;

    struct aim_target_t {
        float   x = 0.f;
        float   y = 0.f;
        float   hit_r = 0.f;
        int32_t hit_time_ms = 0;
        int32_t cur_time_ms = 0;
        bool    valid = false;
    };

    struct aim_assist_config_t {
        bool  enabled = false;
        bool  legit_mode = true;
        bool  use_hitbox = true;
        float strength = 0.5f;
        int   timing_ms = 100;
        float aim_cone_deg = 60.f;
        float idle_threshold_px = 2.f;
        float blend_early = 0.15f;
        float blend_late = 0.85f;
        float return_rate = 0.08f;
        float stick_rate = 0.12f;
        float pre_pull = 0.f;
        float move_blend_rate = 1.f;
    };

    struct aim_assist_state_t {
        float offset_x = 0.f;
        float offset_y = 0.f;
        float smooth_x = 0.f;
        float smooth_y = 0.f;
        float angle_factor = 0.f;
        float angle_secondary = 0.f;
        float move_blend = 0.f;
        float move_blend_rate = 1.f;
        float last_raw_x = 0.f;
        float last_raw_y = 0.f;
        float last_target_x = 0.f;
        float last_target_y = 0.f;
        bool  last_target_valid = false;
        uint64_t last_tick_ms = 0;
    };

    inline float dt_scale( aim_assist_state_t& st ) {
        const uint64_t now = GetTickCount64( );
        float elapsed = k_dt_norm_ms;
        if ( st.last_tick_ms != 0 && now > st.last_tick_ms )
            elapsed = static_cast<float>( now - st.last_tick_ms );
        st.last_tick_ms = now;
        return std::clamp( elapsed / k_dt_norm_ms, 0.01f, 2.5f );
    }

    inline float len2d( float x, float y ) {
        return std::sqrt( x * x + y * y );
    }

    inline float angle_between_deg( float ax, float ay, float bx, float by ) {
        const float la = len2d( ax, ay );
        const float lb = len2d( bx, by );
        if ( la < 1e-5f || lb < 1e-5f )
            return 0.f;
        const float dot = std::clamp( ( ax * bx + ay * by ) / ( la * lb ), -1.f, 1.f );
        return std::acos( dot ) * 57.295776f;
    }

    inline float smooth_follow_k( float dt_norm ) {
        return std::clamp( dt_norm * 0.42f, 0.06f, 0.32f );
    }

    inline float smooth_angle_step( float dt_norm ) {
        return std::clamp( dt_norm * 0.35f, 0.05f, 0.28f );
    }

    inline void decay_offset( aim_assist_state_t& st, float amount ) {
        const float k = std::clamp( amount, 0.f, 1.f );
        st.offset_x *= ( 1.f - k );
        st.offset_y *= ( 1.f - k );
        st.smooth_x = st.offset_x;
        st.smooth_y = st.offset_y;
        st.move_blend = 0.f;
        st.angle_factor = std::max( 0.f, st.angle_factor - k );
        st.angle_secondary = st.angle_factor;
    }

    inline void cap_offset_step( aim_assist_state_t& st, float prev_ox, float prev_oy, float max_step ) {
        float dx = st.offset_x - prev_ox;
        float dy = st.offset_y - prev_oy;
        const float len = len2d( dx, dy );
        if ( len <= max_step || len < 0.001f )
            return;
        const float s = max_step / len;
        st.offset_x = prev_ox + dx * s;
        st.offset_y = prev_oy + dy * s;
        st.smooth_x = st.offset_x;
        st.smooth_y = st.offset_y;
    }

    inline float target_jump_scale(
        float target_x,
        float target_y,
        aim_assist_state_t& st,
        float max_jump ) {
        if ( !st.last_target_valid )
            return 1.f;
        const float dx = target_x - st.last_target_x;
        const float dy = target_y - st.last_target_y;
        const float jump = len2d( dx, dy );
        if ( jump <= max_jump )
            return 1.f;
        return std::clamp( max_jump / jump, 0.12f, 1.f );
    }

    inline void reset_state( aim_assist_state_t& st, float mx, float my ) {
        st = {};
        st.last_raw_x = mx;
        st.last_raw_y = my;
        st.last_tick_ms = GetTickCount64( );
        st.move_blend_rate = 1.f;
        st.last_target_valid = false;
    }

    inline bool apply(
        float mx,
        float my,
        const aim_target_t& target,
        const aim_assist_config_t& cfg,
        aim_assist_state_t& st,
        float* out_x,
        float* out_y ) {

        if ( !out_x || !out_y )
            return false;

        if ( !cfg.enabled ) {
            reset_state( st, mx, my );
            *out_x = mx;
            *out_y = my;
            return true;
        }

        const float dt_norm = dt_scale( st );

        const float user_dx = mx - st.last_raw_x;
        const float user_dy = my - st.last_raw_y;
        const float user_dist = len2d( user_dx, user_dy );

        if ( cfg.legit_mode && user_dist < cfg.idle_threshold_px ) {
            decay_offset( st, std::clamp( cfg.return_rate * dt_norm * 3.f, 0.04f, 0.5f ) );
            st.last_raw_x = mx;
            st.last_raw_y = my;
            *out_x = mx + st.offset_x;
            *out_y = my + st.offset_y;
            return true;
        }

        float target_x = target.x;
        float target_y = target.y;
        float hit_r = target.hit_r;

        const float assisted_x = mx + st.offset_x;
        const float assisted_y = my + st.offset_y;

        bool on_note = false;
        if ( target.valid && cfg.use_hitbox && hit_r > 0.f ) {
            const float dx = target_x - assisted_x;
            const float dy = target_y - assisted_y;
            on_note = len2d( dx, dy ) <= hit_r;
        }

        int window_ms = cfg.timing_ms;
        if ( window_ms < 1 )
            window_ms = 100;

        bool in_window = false;
        int time_until_hit = 0;
        if ( target.valid ) {
            time_until_hit = target.hit_time_ms - target.cur_time_ms;
            in_window = time_until_hit >= 0 && time_until_hit <= window_ms;
        }

        float to_x = target_x - mx;
        float to_y = target_y - my;

        if ( target.valid && cfg.pre_pull > 0.f && !on_note && !in_window ) {
            const float mdx = mx - target_x;
            const float mdy = my - target_y;
            const float md = len2d( mdx, mdy );
            if ( md > 0.1f ) {
                target_x = ( mdx / md ) * cfg.pre_pull * hit_r + target_x;
                target_y = ( mdy / md ) * cfg.pre_pull * hit_r + target_y;
                to_x = target_x - mx;
                to_y = target_y - my;
            }
        }

        float pull_x = 0.f;
        float pull_y = 0.f;

        if ( !target.valid ) {
            decay_offset( st, std::clamp( cfg.return_rate * dt_norm, 0.f, 1.f ) );
            st.last_raw_x = mx;
            st.last_raw_y = my;
            *out_x = mx + st.offset_x;
            *out_y = my + st.offset_y;
            return true;
        }

        if ( target.valid ) {
            to_x = target_x - mx;
            to_y = target_y - my;

            const float jump_scale = target_jump_scale( target_x, target_y, st, 72.f );

            if ( in_window ) {
                float fade = 1.f - static_cast<float>( time_until_hit ) / static_cast<float>( window_ms );
                fade = std::clamp( fade, 0.f, 1.f );
                const float window_strength = fade * cfg.strength;
                const float angle_decay = cfg.return_rate * dt_norm;

                if ( on_note || !cfg.legit_mode ) {
                    st.move_blend = std::max( 0.f, st.move_blend - angle_decay );
                    st.angle_factor = std::max( 0.f, st.angle_secondary - angle_decay );
                    if ( st.angle_factor < 0.f )
                        st.angle_factor = 0.f;
                    pull_x = to_x * window_strength * jump_scale;
                    pull_y = to_y * window_strength * jump_scale;
                }
                else {
                    float move_blend = st.move_blend;
                    bool blocked = false;

                    const float to_len = len2d( to_x, to_y );
                    if ( to_len > 0.1f ) {
                        const float ang = angle_between_deg( user_dx, user_dy, to_x, to_y );
                        if ( cfg.aim_cone_deg < ang )
                            blocked = true;
                    }

                    if ( !blocked ) {
                        move_blend = std::min( 1.f, st.move_blend_rate + user_dist * 0.15f );
                        st.move_blend_rate = move_blend;
                    }

                    move_blend = st.move_blend - angle_decay;
                    if ( move_blend < 0.f )
                        move_blend = 0.f;
                    st.move_blend = move_blend;

                    float gate = st.move_blend;
                    if ( gate < st.angle_factor )
                        gate = st.angle_factor;

                    pull_x = ( 1.f - gate ) * window_strength * to_x * jump_scale;
                    pull_y = ( 1.f - gate ) * window_strength * to_y * jump_scale;
                }
            }
            else if ( cfg.legit_mode ) {
                pull_x = 0.f;
                pull_y = 0.f;
                decay_offset( st, std::clamp( cfg.return_rate * dt_norm * 0.5f, 0.f, 0.2f ) );
            }
            else {
                pull_x = st.offset_x;
                pull_y = st.offset_y;
            }

            st.last_target_x = target_x;
            st.last_target_y = target_y;
            st.last_target_valid = true;
        }
        else {
            st.last_target_valid = false;
        }

        const float smooth_k = smooth_follow_k( dt_norm );
        st.smooth_x += ( pull_x - st.smooth_x ) * smooth_k;
        st.smooth_y += ( pull_y - st.smooth_y ) * smooth_k;

        float freeze_factor = st.angle_factor;
        if ( !target.valid || !cfg.use_hitbox ) {
            freeze_factor = 0.f;
        }
        else if ( !on_note ) {
            freeze_factor = st.angle_factor - cfg.stick_rate * dt_norm;
            if ( freeze_factor < 0.f )
                freeze_factor = 0.f;
            st.angle_factor = freeze_factor;
            st.angle_secondary = st.angle_factor;
        }
        else {
            freeze_factor = st.angle_factor + cfg.stick_rate * dt_norm;
            if ( freeze_factor > 1.f )
                freeze_factor = 1.f;
            st.angle_factor = freeze_factor;
            st.angle_secondary = st.angle_factor;
        }

        float approach_blend = 0.f;
        if ( in_window ) {
            const float t = static_cast<float>( time_until_hit ) / static_cast<float>( window_ms );
            approach_blend = t * ( cfg.blend_late - cfg.blend_early ) + cfg.blend_early;
            approach_blend = std::clamp( approach_blend, 0.f, 1.f );
        }

        float blend = approach_blend * std::clamp( dt_norm * 0.55f, 0.06f, 0.38f );
        blend *= ( 1.f - freeze_factor );

        const float prev_ox = st.offset_x;
        const float prev_oy = st.offset_y;

        st.offset_x += ( st.smooth_x - st.offset_x ) * blend;
        st.offset_y += ( st.smooth_y - st.offset_y ) * blend;

        cap_offset_step( st, prev_ox, prev_oy, 16.f );

        st.last_raw_x = mx;
        st.last_raw_y = my;

        *out_x = mx + st.offset_x;
        *out_y = my + st.offset_y;
        return true;
    }

}