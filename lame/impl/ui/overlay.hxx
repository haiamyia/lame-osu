#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <impl/struct/game_snapshot.hxx>
#include <core/aim_assist/aimbot.hxx>
#include <core/relax/relax.hxx>
#include <core/replay/replay_bot.hxx>
#include <core/autobot/autobot.hxx>
#include <core/threads/cache.hxx>
#include <impl/input/mouse_hook.hxx>
#include <impl/config/config_store.hxx>
#include <functional>
#include <string>
#include <vector>

namespace ui {

    class c_overlay {
    public:
        using snapshot_fn = std::function<osu::full_snapshot_t( )>;

        bool create( HINSTANCE instance );
        void destroy( );
        bool pump( );
        [[nodiscard]] HWND hwnd( ) const { return m_hwnd; }

        void set_snapshot_source( snapshot_fn fn ) { m_snapshot_fn = std::move( fn ); }
        void set_cache( threads::c_cache* cache ) { m_cache = cache; }

        void tick_modules( const osu::full_snapshot_t& snap );
        void reset_modules( const osu::game_snapshot_t& game );
        void apply_custom_keys( osu::game_snapshot_t& game ) const;
        config::settings_t capture_settings( ) const;
        void apply_settings( const config::settings_t& s );

        aim_assist::c_aimbot& aim( ) { return m_aim; }
        relax::c_relax& relax( ) { return m_relax; }
        replay::c_replay_bot& replay( ) { return m_replay; }
        autobot::c_autobot& auto_bot( ) { return m_autobot; }

        bool stream_proof = false;

        static constexpr int MENU_W = 700;
        static constexpr int MENU_H = 420;

    private:
        HWND m_hwnd = nullptr;
        bool m_visible = true;
        bool m_insert_was_down = false;
        snapshot_fn m_snapshot_fn;
        threads::c_cache* m_cache = nullptr;

        aim_assist::c_aimbot m_aim;
        input::c_mouse_hook m_mouse_hook;
        relax::c_relax m_relax;
        replay::c_replay_bot m_replay;
        autobot::c_autobot m_autobot;

        ID3D11Device* m_device = nullptr;
        ID3D11DeviceContext* m_context = nullptr;
        IDXGISwapChain* m_swap_chain = nullptr;
        ID3D11RenderTargetView* m_rtv = nullptr;

        int m_tab = 0;
        char m_replay_path_utf8[ 512 ]{};
        char m_songs_path_utf8[ 512 ]{};
        char m_config_name_utf8[ 128 ]{};
        std::vector<std::string> m_config_profiles;
        std::string m_config_status;
        int m_config_selected = -1;

        osu::game_state_t m_prev_state = osu::game_state_t::unknown;
        int32_t m_prev_map_id = -1;
        int32_t  m_prev_game_time = -1;
        uint64_t m_game_time_stall_start_ms = 0;
        std::string m_prev_map_sig;

        int m_custom_left_key = 'Z';
        int m_custom_right_key = 'X';
        bool m_waiting_left = false;
        bool m_waiting_right = false;

        int m_menu_offset_x = 0;
        int m_menu_offset_y = 0;
        bool m_menu_dragged = false;

        static LRESULT CALLBACK wnd_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp );
        bool init_d3d( );
        void cleanup_d3d( );
        void render_frame( );
        void draw_menu( const osu::full_snapshot_t& snap );
        void update_overlay_position( );
        void apply_visibility( );
        void handle_hotkeys( );
    };

}