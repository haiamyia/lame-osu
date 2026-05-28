#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <impl/memory/win32u.hxx>
#include <Windows.h>

namespace input {

    inline HWND g_target_hwnd = nullptr;
    inline memory::c_win32u_exports g_win32u;
    inline bool g_win32u_ok = false;

    struct virtual_desktop_t {
        int origin_x = 0;
        int origin_y = 0;
        int width = 0;
        int height = 0;
        bool valid = false;
    };

    inline virtual_desktop_t& virtual_desktop( ) {
        static virtual_desktop_t vd{};
        if ( !vd.valid ) {
            vd.origin_x = GetSystemMetrics( SM_XVIRTUALSCREEN );
            vd.origin_y = GetSystemMetrics( SM_YVIRTUALSCREEN );
            vd.width = GetSystemMetrics( SM_CXVIRTUALSCREEN );
            vd.height = GetSystemMetrics( SM_CYVIRTUALSCREEN );
            vd.valid = vd.width > 0 && vd.height > 0;
        }
        return vd;
    }

    inline void invalidate_virtual_desktop( ) {
        virtual_desktop( ).valid = false;
    }

    inline bool init( ) {
        g_win32u_ok = g_win32u.load( ) && g_win32u.bind( );
        invalidate_virtual_desktop( );
        virtual_desktop( );
        return g_win32u_ok;
    }

    inline bool using_nt_input( ) {
        return g_win32u_ok && g_win32u.ready( );
    }

    inline UINT send_inputs( UINT count, LPINPUT inputs, int size ) {
        if ( g_win32u_ok ) {
            const auto fn = g_win32u.send_input( );
            if ( fn )
                return fn( count, inputs, size );
        }
        return ::SendInput( count, inputs, size );
    }

    inline bool get_cursor_pos( LPPOINT point ) {
        if ( g_win32u_ok ) {
            const auto fn = g_win32u.get_cursor_pos( );
            if ( fn )
                return fn( point ) != FALSE;
        }
        return ::GetCursorPos( point ) != FALSE;
    }

    inline bool set_cursor_pos( int x, int y ) {
        if ( g_win32u_ok ) {
            const auto fn = g_win32u.set_cursor_pos( );
            if ( fn )
                return fn( x, y ) != FALSE;
        }
        return ::SetCursorPos( x, y ) != FALSE;
    }

    inline void set_target_window( HWND hwnd ) {
        g_target_hwnd = hwnd;
    }

    inline HWND target_window( ) {
        return g_target_hwnd;
    }

    inline void press_vk( WORD vk ) {
        if ( !vk )
            return;
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        send_inputs( 1, &in, sizeof( INPUT ) );
    }

    inline void release_vk( WORD vk ) {
        if ( !vk )
            return;
        INPUT in{};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        send_inputs( 1, &in, sizeof( INPUT ) );
    }

    inline void key_tap( WORD vk ) {
        press_vk( vk );
        release_vk( vk );
    }

    inline void key_down( WORD vk ) {
        press_vk( vk );
    }

    inline void key_up( WORD vk ) {
        release_vk( vk );
    }

    inline void key_event( WORD vk, bool down ) {
        if ( down )
            press_vk( vk );
        else
            release_vk( vk );
    }

    inline void move_relative( int dx, int dy ) {
        if ( dx == 0 && dy == 0 )
            return;

        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_MOVE;
        in.mi.dx = dx;
        in.mi.dy = dy;
        in.mi.dwExtraInfo = 0xDEADC0DE;
        send_inputs( 1, &in, sizeof( INPUT ) );
    }

    inline bool move_absolute_virtual_desktop( int screen_x, int screen_y ) {
        auto& vd = virtual_desktop( );
        if ( !vd.valid )
            return false;

        const double norm_x = ( static_cast<double>( screen_x - vd.origin_x ) * 65535.0 ) /
                              static_cast<double>( vd.width );
        const double norm_y = ( static_cast<double>( screen_y - vd.origin_y ) * 65535.0 ) /
                              static_cast<double>( vd.height );

        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        in.mi.dx = static_cast<LONG>( norm_x + 0.5 );
        in.mi.dy = static_cast<LONG>( norm_y + 0.5 );
        in.mi.dwExtraInfo = 0xDEADC0DE;
        return send_inputs( 1, &in, sizeof( INPUT ) ) != 0;
    }

    inline void move_absolute( int sx, int sy ) {
        move_absolute_virtual_desktop( sx, sy );
    }

    inline bool move_tablet_assist( int offset_x, int offset_y ) {
        POINT cur{};
        if ( !get_cursor_pos( &cur ) )
            return false;
        return move_absolute_virtual_desktop( cur.x + offset_x, cur.y + offset_y );
    }

    inline void move_to_screen( int sx, int sy ) {
        move_absolute_virtual_desktop( sx, sy );
    }

    inline void snap_cursor_screen( int sx, int sy ) {
        move_absolute_virtual_desktop( sx, sy );
    }

}