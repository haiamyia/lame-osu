#pragma once

#include <Windows.h>
#include <cstring>
#include <cmath>

namespace playfield {

    struct screen_point_t {
        int x = 0;
        int y = 0;
    };

    struct enum_ctx_t {
        DWORD pid = 0;
        HWND best = nullptr;
        LONG best_area = 0;
    };

    inline LONG client_area( HWND hwnd ) {
        RECT client{};
        if ( !GetClientRect( hwnd, &client ) )
            return 0;
        return ( client.right - client.left ) * ( client.bottom - client.top );
    }

    inline BOOL CALLBACK enum_osu_window_proc( HWND hwnd, LPARAM param ) {
        auto* ctx = reinterpret_cast<enum_ctx_t*>( param );
        DWORD window_pid = 0;
        GetWindowThreadProcessId( hwnd, &window_pid );
        if ( window_pid != ctx->pid )
            return TRUE;

        if ( !IsWindowVisible( hwnd ) )
            return TRUE;

        wchar_t cls[ 256 ]{};
        GetClassNameW( hwnd, cls, 256 );

        const bool is_osu_class = wcscmp( cls, L"osu!" ) == 0;
        wchar_t title[ 256 ]{};
        GetWindowTextW( hwnd, title, 256 );
        const bool title_match = wcsstr( title, L"osu" ) != nullptr;

        if ( !is_osu_class && !title_match )
            return TRUE;

        const LONG area = client_area( hwnd );
        if ( area > ctx->best_area ) {
            ctx->best_area = area;
            ctx->best = hwnd;
        }

        return TRUE;
    }

    inline HWND find_osu_window( DWORD pid ) {
        if ( !pid )
            return nullptr;

        enum_ctx_t ctx{ pid, nullptr, 0 };
        EnumWindows( enum_osu_window_proc, reinterpret_cast<LPARAM>( &ctx ) );
        return ctx.best;
    }

    inline bool get_window_screen_rect( HWND hwnd, RECT& out ) {
        if ( !hwnd || !IsWindow( hwnd ) )
            return false;
        if ( !GetWindowRect( hwnd, &out ) )
            return false;
        return out.right > out.left && out.bottom > out.top;
    }

    inline bool get_client_screen_rect( HWND hwnd, RECT& out ) {
        if ( !hwnd || !IsWindow( hwnd ) )
            return false;

        RECT client{};
        if ( !GetClientRect( hwnd, &client ) )
            return false;

        POINT top_left{ client.left, client.top };
        POINT bottom_right{ client.right, client.bottom };
        if ( !ClientToScreen( hwnd, &top_left ) || !ClientToScreen( hwnd, &bottom_right ) )
            return false;

        out.left = top_left.x;
        out.top = top_left.y;
        out.right = bottom_right.x;
        out.bottom = bottom_right.y;
        return out.right > out.left && out.bottom > out.top;
    }

    inline bool get_playfield_rect( HWND hwnd, RECT& out ) {
        return get_client_screen_rect( hwnd, out );
    }

    inline bool get_window_size( HWND hwnd, int32_t& width, int32_t& height ) {
        RECT window{};
        if ( !get_window_screen_rect( hwnd, window ) )
            return false;
        width = window.right - window.left;
        height = window.bottom - window.top;
        return width > 0 && height > 0;
    }

    inline bool get_client_size( HWND hwnd, int32_t& width, int32_t& height ) {
        RECT client{};
        if ( !hwnd || !GetClientRect( hwnd, &client ) )
            return false;
        width = client.right - client.left;
        height = client.bottom - client.top;
        return width > 0 && height > 0;
    }

    inline void project_osu_to_window(
        float x512, float y384, int32_t win_w, int32_t win_h, float& out_x, float& out_y,
        int32_t stack_index = 0, int y_offset = 17 ) {

        const float playfield_height = static_cast<float>( win_h ) * 0.8f;
        const float playfield_width = playfield_height * ( 4.f / 3.f );
        const float osu_scale = playfield_width / 512.f;
        const float offset_x = ( static_cast<float>( win_w ) - playfield_width ) * 0.5f;
        const float offset_y = ( static_cast<float>( win_h ) - playfield_height ) * 0.5f;
        const float stack_offset = -static_cast<float>( stack_index ) * 6.f * osu_scale;

        out_x = offset_x + x512 * osu_scale + stack_offset;
        out_y = offset_y + y384 * osu_scale + stack_offset + static_cast<float>( y_offset );
    }

    inline screen_point_t map_osu_to_screen(
        float x512, float y384, HWND hwnd, int y_offset = 17, int32_t stack_index = 0 ) {

        screen_point_t p{};
        RECT window{};
        if ( !get_playfield_rect( hwnd, window ) )
            return p;

        const int win_w = window.right - window.left;
        const int win_h = window.bottom - window.top;
        if ( win_w <= 1 || win_h <= 1 )
            return p;

        float rel_x = 0.f;
        float rel_y = 0.f;
        project_osu_to_window( x512, y384, win_w, win_h, rel_x, rel_y, stack_index, y_offset );

        p.x = static_cast<int>( window.left + rel_x + 0.5f );
        p.y = static_cast<int>( window.top + rel_y + 0.5f );
        return p;
    }

    inline screen_point_t playfield_to_screen(
        float x512, float y384, const RECT& window, int y_offset = 17 ) {

        const int win_w = window.right - window.left;
        const int win_h = window.bottom - window.top;

        float rel_x = 0.f;
        float rel_y = 0.f;
        project_osu_to_window( x512, y384, win_w, win_h, rel_x, rel_y, 0, y_offset );

        screen_point_t p{};
        p.x = static_cast<int>( window.left + rel_x );
        p.y = static_cast<int>( window.top + rel_y );
        return p;
    }

    inline bool screen_to_playfield( int sx, int sy, const RECT& window, float& x512, float& y384, int y_offset = 17 ) {
        const int win_w = window.right - window.left;
        const int win_h = window.bottom - window.top;
        if ( win_w <= 0 || win_h <= 0 )
            return false;

        const float playfield_height = static_cast<float>( win_h ) * 0.8f;
        const float playfield_width = playfield_height * ( 4.f / 3.f );
        const float osu_scale = playfield_width / 512.f;
        if ( osu_scale <= 0.f )
            return false;

        const float offset_x = ( static_cast<float>( win_w ) - playfield_width ) * 0.5f;
        const float offset_y = ( static_cast<float>( win_h ) - playfield_height ) * 0.5f;

        const float rel_x = static_cast<float>( sx - window.left ) - offset_x;
        const float rel_y = static_cast<float>( sy - window.top ) - offset_y - static_cast<float>( y_offset );

        x512 = rel_x / osu_scale;
        y384 = rel_y / osu_scale;
        return true;
    }

}