#include <impl/ui/overlay.hxx>
#include <impl/ui/theme.hxx>
#include <impl/memory/input.hxx>
#include <impl/util/playfield.hxx>
#include "elements.hxx"
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <commdlg.h>
#include <ShlObj.h>
#include <algorithm>
#include <cstring>

#pragma comment( lib, "d3d11.lib" )
#pragma comment( lib, "dxgi.lib" )
#pragma comment( lib, "d3dcompiler.lib" )
#pragma comment( lib, "dwmapi.lib" )
#pragma comment( lib, "shell32.lib" )
#pragma comment( lib, "ole32.lib" )
#pragma comment( lib, "ws2_32.lib" )

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler( HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam );

namespace {

    bool overlay_aim_hook_transform(
        void* ctx, POINT pen, const MSLLHOOKSTRUCT& raw, POINT* out ) {
        (void)pen;
        if ( !ctx || !out )
            return false;

        auto* overlay = static_cast<ui::c_overlay*>( ctx );
        const auto adjusted = overlay->aim( ).apply_hook_move( pen, raw );
        if ( !adjusted )
            return false;

        *out = *adjusted;
        return true;
    }

    void remove_score_blocker_rule( ) {
        std::wstring del_cmd = L"cmd.exe /c netsh advfirewall firewall delete rule name=\"lame_score_block\"";
        STARTUPINFOW si{};
        si.cb = sizeof( si );
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        PROCESS_INFORMATION pi{};
        if ( CreateProcessW( nullptr, const_cast<LPWSTR>( del_cmd.c_str( ) ), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi ) ) {
            WaitForSingleObject( pi.hProcess, INFINITE );
            CloseHandle( pi.hProcess );
            CloseHandle( pi.hThread );
        }
    }

    std::vector<std::wstring> resolve_host_ips( const std::wstring& host ) {
        std::vector<std::wstring> ips;
        WSADATA wsa_data;
        if ( WSAStartup( MAKEWORD( 2, 2 ), &wsa_data ) != 0 ) {
            return ips;
        }

        ADDRINFOW hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        
        ADDRINFOW* result = nullptr;
        if ( GetAddrInfoW( host.c_str( ), nullptr, &hints, &result ) == 0 ) {
            ADDRINFOW* ptr = result;
            while ( ptr != nullptr ) {
                if ( ptr->ai_family == AF_INET ) {
                    auto* sockaddr_ipv4 = reinterpret_cast<sockaddr_in*>( ptr->ai_addr );
                    wchar_t ip_str[46]{};
                    if ( InetNtopW( AF_INET, &sockaddr_ipv4->sin_addr, ip_str, 46 ) ) {
                        if ( std::find( ips.begin( ), ips.end( ), ip_str ) == ips.end( ) ) {
                            ips.push_back( ip_str );
                        }
                    }
                }
                ptr = ptr->ai_next;
            }
            FreeAddrInfoW( result );
        }
        WSACleanup( );
        return ips;
    }

    void set_score_blocker_state( bool enabled, HANDLE process_handle ) {
        remove_score_blocker_rule( );

        if ( enabled && process_handle ) {
            wchar_t path[MAX_PATH]{};
            DWORD size = MAX_PATH;
            if ( QueryFullProcessImageNameW( process_handle, 0, path, &size ) ) {
                auto ips = resolve_host_ips( L"osu.ppy.sh" );
                if ( !ips.empty( ) ) {
                    std::wstring ip_list;
                    for ( size_t i = 0; i < ips.size( ); ++i ) {
                        if ( i > 0 ) ip_list += L",";
                        ip_list += ips[i];
                    }
                    
                    std::wstring add_cmd = L"cmd.exe /c netsh advfirewall firewall add rule name=\"lame_score_block\" dir=out action=block program=\"" + std::wstring( path ) + L"\" remoteip=\"" + ip_list + L"\" enable=yes";
                    STARTUPINFOW si{};
                    si.cb = sizeof( si );
                    si.dwFlags = STARTF_USESHOWWINDOW;
                    si.wShowWindow = SW_HIDE;
                    PROCESS_INFORMATION pi{};
                    if ( CreateProcessW( nullptr, const_cast<LPWSTR>( add_cmd.c_str( ) ), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi ) ) {
                        WaitForSingleObject( pi.hProcess, INFINITE );
                        CloseHandle( pi.hProcess );
                        CloseHandle( pi.hThread );
                    }
                }
            }
        }
    }

}

namespace ui {

    bool c_overlay::create( HINSTANCE instance ) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof( wc );
        wc.style = CS_CLASSDC;
        wc.lpfnWndProc = wnd_proc;
        wc.hInstance = instance;
        wc.lpszClassName = L"  ";
        RegisterClassExW( &wc );

        m_hwnd = CreateWindowExW(
            WS_EX_APPWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST,
            wc.lpszClassName,
            L"  ",
            WS_POPUP,
            0, 0, MENU_W, MENU_H,
            nullptr, nullptr, instance, this );

        if ( !m_hwnd ) {
            return false;
        }

        MARGINS margins = { -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea( m_hwnd, &margins );

        SetLayeredWindowAttributes( m_hwnd, 0, 255, LWA_ALPHA );

        ShowWindow( m_hwnd, SW_SHOWDEFAULT );
        UpdateWindow( m_hwnd );

        if ( !init_d3d( ) ) {
            return false;
        }

        IMGUI_CHECKVERSION( );
        ImGui::CreateContext( );
        ImGuiIO& io = ImGui::GetIO( );
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImFontConfig font_cfg{};
        font_cfg.OversampleH = 3;
        font_cfg.OversampleV = 3;
        font_cfg.PixelSnapH = true;
        io.Fonts->AddFontFromFileTTF( "C:\\Windows\\Fonts\\segoeui.ttf", 16.f, &font_cfg );

        ui::theme::apply_style( );

        ImGui_ImplWin32_Init( m_hwnd );
        ImGui_ImplDX11_Init( m_device, m_context );

        m_mouse_hook.set_filter_injected_only( true );
        m_mouse_hook.set_transform( overlay_aim_hook_transform, this );
        m_mouse_hook.install( );

        return true;
    }

    void c_overlay::destroy( ) {
        m_mouse_hook.uninstall( );
        remove_score_blocker_rule( );

        ImGui_ImplDX11_Shutdown( );
        ImGui_ImplWin32_Shutdown( );
        ImGui::DestroyContext( );
        cleanup_d3d( );
        if ( m_hwnd ) {
            DestroyWindow( m_hwnd );
            m_hwnd = nullptr;
        }
    }

    bool c_overlay::pump( ) {
        MSG msg;
        while ( PeekMessage( &msg, nullptr, 0, 0, PM_REMOVE ) ) {
            TranslateMessage( &msg );
            DispatchMessage( &msg );
            if ( msg.message == WM_QUIT )
                return false;
        }

        if ( stream_proof )
            SetWindowDisplayAffinity( m_hwnd, WDA_EXCLUDEFROMCAPTURE );
        else
            SetWindowDisplayAffinity( m_hwnd, WDA_NONE );

        handle_hotkeys( );
        update_overlay_position( );
        apply_visibility( );
        render_frame( );
        return true;
    }

    void c_overlay::handle_hotkeys( ) {
        const bool insert_down = ( GetAsyncKeyState( VK_INSERT ) & 0x8000 ) != 0;
        if ( insert_down && !m_insert_was_down )
            m_visible = !m_visible;
        m_insert_was_down = insert_down;
    }

    void c_overlay::update_overlay_position( ) {
        if ( !m_hwnd )
            return;

        const HWND osu_hwnd = input::target_window( );
        input::invalidate_virtual_desktop( );
        input::virtual_desktop( );

        int target_x = 0;
        int target_y = 0;

        if ( !osu_hwnd || !IsWindow( osu_hwnd ) ) {
            const int screen_w = GetSystemMetrics( SM_CXSCREEN );
            const int screen_h = GetSystemMetrics( SM_CYSCREEN );
            target_x = ( screen_w - static_cast<int>( MENU_W ) ) / 2;
            target_y = ( screen_h - static_cast<int>( MENU_H ) ) / 2;
        }
        else {
            RECT client{};
            if ( playfield::get_playfield_rect( osu_hwnd, client ) ) {
                target_x = client.left + ( ( client.right - client.left ) - static_cast<int>( MENU_W ) ) / 2;
                target_y = client.top + ( ( client.bottom - client.top ) - static_cast<int>( MENU_H ) ) / 2;
            }
            else {
                return;
            }
        }

        target_x += m_menu_offset_x;
        target_y += m_menu_offset_y;

        SetWindowPos( m_hwnd, HWND_TOPMOST, target_x, target_y, static_cast<int>( MENU_W ), static_cast<int>( MENU_H ), SWP_NOACTIVATE );
    }

    void c_overlay::apply_visibility( ) {
        if ( !m_hwnd )
            return;

        if ( m_visible ) {
            ShowWindow( m_hwnd, SW_SHOWNA );
            LONG ex = GetWindowLongW( m_hwnd, GWL_EXSTYLE );
            ex &= ~WS_EX_TRANSPARENT;
            SetWindowLongW( m_hwnd, GWL_EXSTYLE, ex );
        }
        else {
            ShowWindow( m_hwnd, SW_HIDE );
            LONG ex = GetWindowLongW( m_hwnd, GWL_EXSTYLE );
            ex |= WS_EX_TRANSPARENT;
            SetWindowLongW( m_hwnd, GWL_EXSTYLE, ex );
        }
    }

    LRESULT CALLBACK c_overlay::wnd_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp ) {
        if ( ImGui_ImplWin32_WndProcHandler( hwnd, msg, wp, lp ) )
            return true;

        if ( msg == WM_NCCREATE ) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>( lp );
            SetWindowLongPtrW( hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>( cs->lpCreateParams ) );
        }

        auto* self = reinterpret_cast<c_overlay*>( GetWindowLongPtrW( hwnd, GWLP_USERDATA ) );
        if ( msg == WM_DESTROY ) {
            PostQuitMessage( 0 );
            return 0;
        }
        if ( msg == WM_SIZE && self && self->m_swap_chain ) {
            if ( self->m_rtv ) {
                self->m_rtv->Release( );
                self->m_rtv = nullptr;
            }
            self->m_swap_chain->ResizeBuffers( 0, LOWORD( lp ), HIWORD( lp ), DXGI_FORMAT_UNKNOWN, 0 );
            ID3D11Texture2D* back = nullptr;
            self->m_swap_chain->GetBuffer( 0, IID_PPV_ARGS( &back ) );
            if ( back ) {
                self->m_device->CreateRenderTargetView( back, nullptr, &self->m_rtv );
                back->Release( );
            }
        }
        return DefWindowProcW( hwnd, msg, wp, lp );
    }

    bool c_overlay::init_d3d( ) {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = m_hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        D3D_FEATURE_LEVEL level{};
        if ( D3D11CreateDeviceAndSwapChain(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0,
                D3D11_SDK_VERSION, &sd, &m_swap_chain, &m_device, &level, &m_context ) != S_OK )
            return false;

        ID3D11Texture2D* back = nullptr;
        m_swap_chain->GetBuffer( 0, IID_PPV_ARGS( &back ) );
        if ( !back )
            return false;
        m_device->CreateRenderTargetView( back, nullptr, &m_rtv );
        back->Release( );
        return m_rtv != nullptr;
    }

    void c_overlay::cleanup_d3d( ) {
        if ( m_rtv ) {
            m_rtv->Release( );
            m_rtv = nullptr;
        }
        if ( m_swap_chain ) {
            m_swap_chain->Release( );
            m_swap_chain = nullptr;
        }
        if ( m_context ) {
            m_context->Release( );
            m_context = nullptr;
        }
        if ( m_device ) {
            m_device->Release( );
            m_device = nullptr;
        }
    }

    void c_overlay::render_frame( ) {
        if ( !m_visible ) {
            const float clear[ 4 ]{ 0.0f, 0.0f, 0.0f, 0.0f };
            m_context->OMSetRenderTargets( 1, &m_rtv, nullptr );
            m_context->ClearRenderTargetView( m_rtv, clear );
            m_swap_chain->Present( 0, 0 );
            return;
        }

        ImGui_ImplDX11_NewFrame( );
        ImGui_ImplWin32_NewFrame( );
        ImGui::NewFrame( );

        osu::full_snapshot_t snap;
        if ( m_snapshot_fn )
            snap = m_snapshot_fn( );

        draw_menu( snap );

        ImGui::Render( );
        const float clear[ 4 ]{ 0.0f, 0.0f, 0.0f, 0.0f };
        m_context->OMSetRenderTargets( 1, &m_rtv, nullptr );
        m_context->ClearRenderTargetView( m_rtv, clear );
        ImGui_ImplDX11_RenderDrawData( ImGui::GetDrawData( ) );
        m_swap_chain->Present( 0, 0 );
    }

    void c_overlay::reset_modules( const osu::game_snapshot_t& game ) {
        m_game_time_stall_start_ms = 0;
        m_aim.set_user_input_blocked( false );
        m_aim.on_leave_play( );
        osu::game_snapshot_t mod_game = game;
        if ( mod_game.client == osu::client_kind_t::lazer ) {
            mod_game.left_key = m_custom_left_key;
            mod_game.right_key = m_custom_right_key;
        }
        m_relax.on_leave_play( mod_game );
        m_replay.on_leave_play( mod_game );
        m_autobot.on_leave_play( mod_game );
    }

    void c_overlay::tick_modules( const osu::full_snapshot_t& snap ) {
        const bool in_play = snap.game.cur_state == osu::game_state_t::play;
        const bool was_play = m_prev_state == osu::game_state_t::play;

        const std::string map_sig =
            std::to_string( snap.game.map_id ) + "|" + std::to_string( snap.game.set_id ) + "|" +
            snap.game.map_folder + "|" + snap.game.map_file + "|" + snap.game.beatmap_hash + "|" +
            snap.game.beatmap_version;

        if ( was_play && !in_play ) {
            reset_modules( snap.game );
            m_prev_game_time = -1;
            m_prev_map_id = -1;
            m_prev_map_sig.clear( );
            m_game_time_stall_start_ms = 0;
        }

        if ( in_play && !map_sig.empty( ) && !m_prev_map_sig.empty( ) && map_sig != m_prev_map_sig )
            reset_modules( snap.game );

        if ( in_play && m_prev_map_id > 0 && snap.game.map_id != 0 && snap.game.map_id != m_prev_map_id )
            reset_modules( snap.game );

        if ( was_play && in_play && m_prev_game_time >= 0 &&
             snap.game.cur_time < m_prev_game_time - 200 )
            reset_modules( snap.game );

        m_prev_state = snap.game.cur_state;

        if ( in_play && snap.beatmap.loaded && !snap.beatmap.objects.empty( ) ) {
            osu::full_snapshot_t mod_snap = snap;
            if ( mod_snap.game.client == osu::client_kind_t::lazer ) {
                mod_snap.game.left_key = m_custom_left_key;
                mod_snap.game.right_key = m_custom_right_key;
            }

            constexpr uint64_t k_pause_stall_ms = 120;
            const uint64_t     now_ms = ::GetTickCount64( );
            const int32_t      cur_time = mod_snap.game.cur_time;
            const bool         time_stalled =
                m_prev_game_time >= 0 && cur_time == m_prev_game_time;

            if ( time_stalled ) {
                if ( m_game_time_stall_start_ms == 0 )
                    m_game_time_stall_start_ms = now_ms;
            }
            else {
                m_game_time_stall_start_ms = 0;
            }

            const bool map_paused = m_game_time_stall_start_ms != 0
                                    && ( now_ms - m_game_time_stall_start_ms ) >= k_pause_stall_ms;

            m_aim.set_user_input_blocked( map_paused );

            if ( !map_paused && !( m_replay.enabled && m_replay.disable_aim ) )
                m_aim.update( mod_snap.game, mod_snap.beatmap );

            m_relax.update( mod_snap.game, mod_snap.beatmap );
            m_replay.update( mod_snap.game, mod_snap.beatmap, map_paused );
            m_autobot.update( mod_snap.game, mod_snap.beatmap, map_paused );
        }
        else {
            m_game_time_stall_start_ms = 0;
            m_aim.set_user_input_blocked( false );
        }

        if ( in_play ) {
            m_prev_map_id = snap.game.map_id;
            m_prev_game_time = snap.game.cur_time;
            if ( !map_sig.empty( ) )
                m_prev_map_sig = map_sig;
        }
    }

    static void draw_card( ImDrawList* dl, ImVec2 wpos, float lx, float box_top, float box_bottom, float w, const char* label, ImU32 label_col ) {
        auto S = [&]( float x, float y ) { return ImVec2( wpos.x + x, wpos.y + y ); };
        const ImVec2 p0 = S( lx, box_top );
        const ImVec2 p1 = S( lx + w, box_bottom );

        dl->AddRectFilled( p0, p1, IM_COL32( 18, 18, 23, 235 ), 6.0f );
        dl->AddRect( p0, p1, IM_COL32( 36, 36, 46, 255 ), 6.0f, 0, 1.0f );

        draw_gradient_line( dl, p0.x + 4.0f, p0.y, w - 8.0f, 1.5f, colors::accent );
        dl->AddText( S( lx + 10.0f, box_top + 6.0f ), label_col, label );
    }

    void c_overlay::draw_menu( const osu::full_snapshot_t& snap ) {
        update_bind_capture( );

        static const float MENU_W    = 700.0f;
        static const float MENU_H    = 420.0f;
        static const float SIDEBAR_W = 160.0f;
        static const float TITLE_H   = 28.0f;
        static const float PADDING   = 16.0f;
        static const float L_X       = SIDEBAR_W + PADDING;
        static const float L_W       = 246.0f;
        static const float GAP       = 16.0f;
        static const float R_X       = L_X + L_W + GAP;
        static const float R_W       = 246.0f;

        {
            const ImVec4 mc = ImGui::ColorConvertU32ToFloat4( IM_COL32( 160, 100, 255, 255 ) );
            colors::accent = ImGui::ColorConvertFloat4ToU32( mc );
            const ImVec4 hov( ImMin( mc.x * 1.12f, 1.0f ), ImMin( mc.y * 1.12f, 1.0f ), ImMin( mc.z * 1.12f, 1.0f ), mc.w );
            colors::accent_hover = ImGui::ColorConvertFloat4ToU32( hov );
            const ImVec4 dark( mc.x * 0.28f, mc.y * 0.28f, mc.z * 0.28f, mc.w );
            colors::accent_dark = ImGui::ColorConvertFloat4ToU32( dark );
            const ImVec4 bh( mc.x * 0.88f, mc.y * 0.88f, mc.z * 0.88f, mc.w );
            colors::cb_border_hov = ImGui::ColorConvertFloat4ToU32( bh );
        }

        ImGuiIO& io = ImGui::GetIO( );
        ImGui::SetNextWindowPos( ImVec2( 0.0f, 0.0f ), ImGuiCond_Always );
        ImGui::SetNextWindowSize( ImVec2( MENU_W, MENU_H ), ImGuiCond_Always );

        ImGuiWindowFlags wf =
            ImGuiWindowFlags_NoTitleBar        |
            ImGuiWindowFlags_NoResize          |
            ImGuiWindowFlags_NoScrollbar       |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoBackground      |
            ImGuiWindowFlags_NoBringToFrontOnFocus |
            ImGuiWindowFlags_NoMove;

        ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0, 0 ) );
        ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing,   ImVec2( 0, 0 ) );
        ImGui::Begin( "##lame", nullptr, wf );
        ImGui::PopStyleVar( 2 );

        ImDrawList* dl = ImGui::GetWindowDrawList( );
        const ImVec2 wpos = ImGui::GetWindowPos( );
        auto S = [&]( float x, float y ) { return ImVec2( wpos.x + x, wpos.y + y ); };

        dl->AddRectFilled( wpos, ImVec2( wpos.x + MENU_W, wpos.y + MENU_H ), colors::bg, 12.0f );

        dl->AddRectFilled( wpos, ImVec2( wpos.x + SIDEBAR_W, wpos.y + MENU_H ), IM_COL32( 14, 14, 19, 255 ), 12.0f, ImDrawFlags_RoundCornersLeft );
        dl->AddLine( ImVec2( wpos.x + SIDEBAR_W, wpos.y ), ImVec2( wpos.x + SIDEBAR_W, wpos.y + MENU_H ), IM_COL32( 30, 30, 38, 255 ), 1.0f );

        {
            const char* logo_text = "LAME";
            const ImVec2 logo_size = ImGui::CalcTextSize( logo_text );
            dl->AddText( ImVec2( wpos.x + ( SIDEBAR_W - logo_size.x ) * 0.5f, wpos.y + 22.0f ), colors::accent, logo_text );

            const char* sub_text = "osu! cheetos";
            const ImVec2 sub_size = ImGui::CalcTextSize( sub_text );
            dl->AddText( ImVec2( wpos.x + ( SIDEBAR_W - sub_size.x ) * 0.5f, wpos.y + 40.0f ), colors::text_dim, sub_text );
        }

        {
            static const char* tab_names[ ] = { "Aimbot", "Relax", "Replay", "Autobot", "System" };
            const float tab_start_y = wpos.y + 80.0f;
            const float tab_h = 36.0f;
            const float tab_gap = 10.0f;

            const ImVec2 mouse = ImGui::GetIO( ).MousePos;
            const bool mouse_clicked = ImGui::IsMouseClicked( ImGuiMouseButton_Left );

            for ( int i = 0; i < 5; i++ ) {
                const ImVec2 btn_min = ImVec2( wpos.x + 12.0f, tab_start_y + static_cast<float>( i ) * ( tab_h + tab_gap ) );
                const ImVec2 btn_max = ImVec2( btn_min.x + ( SIDEBAR_W - 24.0f ), btn_min.y + tab_h );

                const bool hov = ( mouse.x >= btn_min.x && mouse.x <= btn_max.x && mouse.y >= btn_min.y && mouse.y <= btn_max.y );
                const bool active = ( i == m_tab );

                if ( hov && mouse_clicked ) {
                    m_tab = i;
                }

                static std::unordered_map<int, float> tab_hover_anims;
                float& anim = tab_hover_anims[ i ];
                anim += ( ( ( hov || active ) ? 1.0f : 0.0f ) - anim ) * io.DeltaTime * 12.0f;

                if ( anim > 0.01f ) {
                    const ImU32 bg_col = ImGui::ColorConvertFloat4ToU32( ImVec4( 26.f / 255.f, 24.f / 255.f, 38.f / 255.f, 0.7f * anim ) );
                    dl->AddRectFilled( btn_min, btn_max, bg_col, 6.0f );

                    if ( active ) {
                        dl->AddRectFilled( ImVec2( btn_min.x, btn_min.y + 6.0f ), ImVec2( btn_min.x + 3.0f, btn_max.y - 6.0f ), colors::accent, 1.5f );
                    }
                }

                const ImVec2 ts = ImGui::CalcTextSize( tab_names[ i ] );
                const ImVec2 tp = ImVec2( active ? ( btn_min.x + 16.0f ) : ( btn_min.x + 12.0f ), btn_min.y + ( tab_h - ts.y ) * 0.5f );

                const ImU32 text_col = active ? colors::text_bright : ( hov ? colors::text : colors::text_dim );
                dl->AddText( tp, text_col, tab_names[ i ] );
            }
        }

        dl->AddRectFilled( ImVec2( wpos.x + SIDEBAR_W + 1.0f, wpos.y ), ImVec2( wpos.x + MENU_W, wpos.y + TITLE_H ), colors::title_bg, 12.0f, ImDrawFlags_RoundCornersTopRight );

        const ImVec2 grad_top = ImVec2( wpos.x + SIDEBAR_W + 1.0f, wpos.y );
        const ImVec2 grad_bot = ImVec2( wpos.x + MENU_W, wpos.y + TITLE_H );
        dl->AddRectFilledMultiColor( grad_top, grad_bot, IM_COL32( 24, 24, 32, 255 ), IM_COL32( 24, 24, 32, 255 ), IM_COL32( 11, 11, 14, 255 ), IM_COL32( 11, 11, 14, 255 ) );

        draw_gradient_line( dl, wpos.x + SIDEBAR_W + 1.0f, wpos.y + TITLE_H, MENU_W - SIDEBAR_W - 1.0f, 1.5f );

        ImGui::SetCursorPos( ImVec2( SIDEBAR_W + 1.0f, 0 ) );
        ImGui::InvisibleButton( "##titlebar", ImVec2( MENU_W - SIDEBAR_W - 35.0f, TITLE_H ) );
        if ( ImGui::IsItemActive( ) && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) ) {
            ReleaseCapture( );
            SendMessageW( m_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0 );

            RECT rect{};
            if ( GetWindowRect( m_hwnd, &rect ) ) {
                const HWND osu_hwnd = input::target_window( );
                int center_x = 0;
                int center_y = 0;

                if ( !osu_hwnd || !IsWindow( osu_hwnd ) ) {
                    const int screen_w = GetSystemMetrics( SM_CXSCREEN );
                    const int screen_h = GetSystemMetrics( SM_CYSCREEN );
                    center_x = ( screen_w - static_cast<int>( MENU_W ) ) / 2;
                    center_y = ( screen_h - static_cast<int>( MENU_H ) ) / 2;
                }
                else {
                    RECT client{};
                    if ( playfield::get_playfield_rect( osu_hwnd, client ) ) {
                        center_x = client.left + ( ( client.right - client.left ) - static_cast<int>( MENU_W ) ) / 2;
                        center_y = client.top + ( ( client.bottom - client.top ) - static_cast<int>( MENU_H ) ) / 2;
                    }
                }

                m_menu_offset_x = rect.left - center_x;
                m_menu_offset_y = rect.top - center_y;
                m_menu_dragged = true;
            }
        }

        const float CLOSE_W = 18.0f;
        const float CLOSE_H = TITLE_H;
        ImGui::SetCursorPos( ImVec2( MENU_W - CLOSE_W - 10.0f, 0.0f ) );
        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 0, 0, 0, 0 ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( 0, 0, 0, 0 ) );
        ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( 0, 0, 0, 0 ) );
        ImGui::PushStyleColor( ImGuiCol_Text, colors::text_dim );
        if ( ImGui::Button( "X##close", ImVec2( CLOSE_W, CLOSE_H ) ) ) {
            PostMessageW( m_hwnd, WM_CLOSE, 0, 0 );
        }
        ImGui::PopStyleColor( 4 );

        if (m_tab == 0) {
            const float lbox_top = TITLE_H + 12.0f;
            float ly = lbox_top + 28.0f;
            dl->ChannelsSplit(2);
            dl->ChannelsSetCurrent(1);
            ImGui::SetCursorPos(ImVec2(L_X + 10.0f, ly));
            checkbox("Enable aim assist", &m_aim.enabled);
            ly = ImGui::GetCursorPos().y + 4.0f;
            ImGui::SetCursorPos(ImVec2(L_X + 10.0f, ly));
            checkbox("Ignore sliders", &m_aim.ignore_sliders);
            ly = ImGui::GetCursorPos().y + 4.0f;
            ImGui::SetCursorPos(ImVec2(L_X + 10.0f, ly));
            checkbox("Legit mode", &m_aim.legit_mode);
            ly = ImGui::GetCursorPos().y + 4.0f;
            ImGui::SetCursorPos(ImVec2(L_X + 10.0f, ly));
            checkbox("Use hitbox", &m_aim.use_hitbox);
            ly = ImGui::GetCursorPos().y + 4.0f;
            ImGui::SetCursorPos(ImVec2(L_X + 10.0f, ly));
            checkbox("Tablet mode", &m_aim.tablet_mode);
            ly = ImGui::GetCursorPos().y + 4.0f;
            const float lbox_bottom = ly + 10.0f;
            dl->ChannelsSetCurrent(0);
            draw_card(dl, wpos, L_X, lbox_top, lbox_bottom, L_W, "aim assist", colors::col_hdr);
            dl->ChannelsMerge();

            const float rbox_top = TITLE_H + 12.0f;
            float ry = rbox_top + 28.0f;
            dl->ChannelsSplit(2);
            dl->ChannelsSetCurrent(1);
            ImGui::SetCursorPos(ImVec2(R_X + 10.0f, ry));
            slider_float("Strength", &m_aim.strength, 0.f, 12.f, "", "%.1f");
            ry = ImGui::GetCursorPos().y + 3.0f;
            ImGui::SetCursorPos(ImVec2(R_X + 10.0f, ry));
            slider_int("Timing window", &m_aim.timing_ms, 10, 300, "%d ms");
            ry = ImGui::GetCursorPos().y + 3.0f;
            ImGui::SetCursorPos(ImVec2(R_X + 10.0f, ry));
            slider_float("Aim cone", &m_aim.aim_cone_deg, 0.f, 180.f, "", "%.0f");
            ry = ImGui::GetCursorPos().y + 3.0f;
            ImGui::SetCursorPos(ImVec2(R_X + 10.0f, ry));
            slider_float("Idle threshold", &m_aim.idle_threshold_px, 0.f, 20.f, "", "%.1f");
            ry = ImGui::GetCursorPos().y + 3.0f;
            ImGui::SetCursorPos(ImVec2(R_X + 10.0f, ry));
            slider_float("Blend (early)", &m_aim.blend_early, 0.f, 1.f, "", "%.2f");
            ry = ImGui::GetCursorPos().y + 3.0f;
            ImGui::SetCursorPos(ImVec2(R_X + 10.0f, ry));
            slider_float("Blend (late)", &m_aim.blend_late, 0.f, 1.f, "", "%.2f");
            ry = ImGui::GetCursorPos().y + 3.0f;
            const float rbox_bottom = ry + 10.0f;
            dl->ChannelsSetCurrent(0);
            draw_card(dl, wpos, R_X, rbox_top, rbox_bottom, R_W, "aim assist tuning", colors::col_hdr);
            dl->ChannelsMerge();
        }
        else if ( m_tab == 1 ) {
            const float lbox_top = TITLE_H + 12.0f;
            float ly = lbox_top + 28.0f;

            dl->ChannelsSplit( 2 );
            dl->ChannelsSetCurrent( 1 );

            ImGui::SetCursorPos( ImVec2( L_X + 10.0f, ly ) );
            checkbox( "Enable relax", &m_relax.enabled );
            ly = ImGui::GetCursorPos( ).y + 4.0f;

            ImGui::SetCursorPos( ImVec2( L_X + 10.0f, ly ) );
            slider_float( "Hit window", &m_relax.hit_window_ms, 0.f, 40.f, " ms" );
            ly = ImGui::GetCursorPos( ).y + 4.0f;

            ImGui::SetCursorPos( ImVec2( L_X + 10.0f, ly ) );
            slider_int( "Manual offset", &m_relax.manual_offset_ms, -100, 100, " ms" );
            ly = ImGui::GetCursorPos( ).y + 8.0f;

            {
                bool is_singletap = ( m_relax.tap_style == 1 );
                ImGui::SetCursorPos( ImVec2( L_X + 10.0f, ly ) );
                checkbox( "Singletap mode", &is_singletap );
                m_relax.tap_style = is_singletap ? 1 : 0;
            }
            ly = ImGui::GetCursorPos( ).y + 4.0f;

            ImGui::SetCursorPos( ImVec2( L_X + 10.0f, ly ) );
            slider_int( "Max ST BPM", &m_relax.singletap_bpm_cap, 100, 300, " bpm" );
            ly = ImGui::GetCursorPos( ).y;

            const float lbox_bottom = ly + 10.0f;
            dl->ChannelsSetCurrent( 0 );
            draw_card( dl, wpos, L_X, lbox_top, lbox_bottom, L_W, "relax options", colors::col_hdr );
            dl->ChannelsMerge( );

            const float rbox_top = TITLE_H + 12.0f;
            float ry = rbox_top + 28.0f;

            dl->ChannelsSplit( 2 );
            dl->ChannelsSetCurrent( 1 );

            dl->AddText( S( R_X + 12.0f, ry ), colors::text_bright, "K1 Hold Shape" );
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 10.0f, ry ) );
            slider_float( "K1 Center", &m_relax.k1_hold_center, 30.f, 120.f, " ms" );
            ry = ImGui::GetCursorPos( ).y + 3.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 10.0f, ry ) );
            slider_float( "K1 Spread", &m_relax.k1_hold_spread, 2.f, 30.f, " ms" );
            ry = ImGui::GetCursorPos( ).y + 8.0f;

            dl->AddText( S( R_X + 12.0f, ry ), colors::text_bright, "K2 Hold Shape" );
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 10.0f, ry ) );
            slider_float( "K2 Center", &m_relax.k2_hold_center, 30.f, 120.f, " ms" );
            ry = ImGui::GetCursorPos( ).y + 3.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 10.0f, ry ) );
            slider_float( "K2 Spread", &m_relax.k2_hold_spread, 2.f, 30.f, " ms" );
            ry = ImGui::GetCursorPos( ).y + 8.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 10.0f, ry ) );
            slider_float( "Hold floor", &m_relax.hold_floor, 10.f, 60.f, " ms" );
            ry = ImGui::GetCursorPos( ).y + 3.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 10.0f, ry ) );
            slider_float( "Hold ceiling", &m_relax.hold_ceiling, 60.f, 150.f, " ms" );
            ry = ImGui::GetCursorPos( ).y + 8.0f;

            if ( m_relax.is_active( ) ) {
                dl->AddText( S( R_X + 12.0f, ry ), IM_COL32( 100, 255, 100, 255 ), "Status: Running" );
            }
            else if ( m_relax.is_synced( ) && m_relax.enabled ) {
                dl->AddText( S( R_X + 12.0f, ry ), IM_COL32( 255, 200, 100, 255 ), "Status: Synced" );
            }
            else {
                dl->AddText( S( R_X + 12.0f, ry ), colors::text_dim, "Status: Idle" );
            }
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            if ( m_relax.enabled && snap.beatmap.loaded ) {
                char buf[ 64 ];
                sprintf_s( buf, "Hit object: %d / %zu", m_relax.last_hit_obj_idx( ), snap.beatmap.objects.size( ) );
                dl->AddText( S( R_X + 12.0f, ry ), colors::text, buf );
                ry += ImGui::GetTextLineHeight( );
            }

            const float rbox_bottom = ry + 10.0f;
            dl->ChannelsSetCurrent( 0 );
            draw_card( dl, wpos, R_X, rbox_top, rbox_bottom, R_W, "hold times & status", colors::col_hdr );
            dl->ChannelsMerge( );
        }
        else if ( m_tab == 2 ) {
            const float lbox_top = TITLE_H + 12.0f;
            float ly = lbox_top + 28.0f;

            dl->ChannelsSplit( 2 );
            dl->ChannelsSetCurrent( 1 );

            ImGui::SetCursorPos( ImVec2( L_X + 10.0f, ly ) );
            checkbox( "Enable replay bot", &m_replay.enabled );
            if ( ImGui::IsItemClicked( ) && m_replay.enabled ) {
                m_replay.reset_sync( );
            }
            ly = ImGui::GetCursorPos( ).y + 6.0f;

            dl->AddText( S( L_X + 10.0f, ly ), colors::text_dim, "Replay path:" );
            ly += ImGui::GetTextLineHeight( ) + 4.0f;

            ImGui::SetCursorPos( ImVec2( L_X + 10.0f, ly ) );
            text_input( "##replay_path", m_replay_path_utf8, IM_ARRAYSIZE( m_replay_path_utf8 ), L_W - 20.0f );
            ly = ImGui::GetCursorPos( ).y + 6.0f;

            ImGui::SetCursorPos( ImVec2( L_X + 10.0f, ly ) );
            if ( button( "Browse", L_W - 20.0f, 20.0f ) ) {
                OPENFILENAMEW ofn{};
                wchar_t file[ 512 ]{};
                ofn.lStructSize = sizeof( ofn );
                ofn.hwndOwner = m_hwnd;
                ofn.lpstrFilter = L"Replay Files\0*.osr\0All\0*.*\0";
                ofn.lpstrFile = file;
                ofn.nMaxFile = 512;
                ofn.Flags = OFN_FILEMUSTEXIST;
                if ( GetOpenFileNameW( &ofn ) ) {
                    WideCharToMultiByte( CP_UTF8, 0, file, -1, m_replay_path_utf8, IM_ARRAYSIZE( m_replay_path_utf8 ), nullptr, nullptr );
                    m_replay.replay_path.assign( file );
                    m_replay.load_replay( );
                    m_replay.reset_sync( );
                }
            }
            ly = ImGui::GetCursorPos( ).y + 4.0f;

            ImGui::SetCursorPos( ImVec2( L_X + 10.0f, ly ) );
            if ( button( "Load Replay", L_W - 20.0f, 20.0f ) ) {
                wchar_t wide[ 512 ]{};
                MultiByteToWideChar( CP_UTF8, 0, m_replay_path_utf8, -1, wide, 512 );
                m_replay.replay_path = wide;
                m_replay.load_replay( );
                m_replay.reset_sync( );
            }
            ly = ImGui::GetCursorPos( ).y;

            const float lbox_bottom = ly + 10.0f;
            dl->ChannelsSetCurrent( 0 );
            draw_card( dl, wpos, L_X, lbox_top, lbox_bottom, L_W, "replay loading", colors::col_hdr );
            dl->ChannelsMerge( );

            const float rbox_top = TITLE_H + 12.0f;
            float ry = rbox_top + 28.0f;

            dl->ChannelsSplit( 2 );
            dl->ChannelsSetCurrent( 1 );

            ImGui::SetCursorPos( ImVec2( R_X + 10.0f, ry ) );
            slider_float( "Speed multiplier", &m_replay.speed_multiplier, 0.5f, 2.f, "x", "%.2f" );
            ry = ImGui::GetCursorPos( ).y + 3.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 10.0f, ry ) );
            slider_int( "Time offset", &m_replay.time_offset_ms, -500, 500, "ms" );
            ry = ImGui::GetCursorPos( ).y + 3.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 10.0f, ry ) );
            slider_int( "Y offset", &m_replay.y_playfield_offset, -50, 50, "px" );
            ry = ImGui::GetCursorPos( ).y + 3.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 10.0f, ry ) );
            checkbox( "Flip replay", &m_replay.flip_replay );
            ry = ImGui::GetCursorPos( ).y + 4.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 10.0f, ry ) );
            checkbox( "Disable aim", &m_replay.disable_aim );
            ry = ImGui::GetCursorPos( ).y + 4.0f;

            ImGui::SetCursorPos( ImVec2( R_X + 10.0f, ry ) );
            checkbox( "Disable clicking", &m_replay.disable_clicking );
            ry = ImGui::GetCursorPos( ).y + 8.0f;

            char buf[ 64 ];
            sprintf_s( buf, "Frames: %zu", m_replay.frame_count( ) );
            dl->AddText( S( R_X + 12.0f, ry ), colors::text, buf );
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            sprintf_s( buf, "Valid: %s", m_replay.replay_valid( ) ? "yes" : "no" );
            dl->AddText( S( R_X + 12.0f, ry ), m_replay.replay_valid( ) ? IM_COL32( 100, 255, 100, 255 ) : IM_COL32( 255, 100, 100, 255 ), buf );
            ry += ImGui::GetTextLineHeight( );

            if ( !m_replay.last_load_error( ).empty( ) ) {
                ry += 4.0f;
                dl->AddText( S( R_X + 12.0f, ry ), IM_COL32( 255, 100, 100, 255 ), m_replay.last_load_error( ).c_str( ) );
                ry += ImGui::GetTextLineHeight( );
            }

            const float rbox_bottom = ry + 10.0f;
            dl->ChannelsSetCurrent( 0 );
            draw_card( dl, wpos, R_X, rbox_top, rbox_bottom, R_W, "replay options", colors::col_hdr );
            dl->ChannelsMerge( );
        }
        else if ( m_tab == 3 ) {
            const float lbox_top = TITLE_H + 12.0f;
            float ly = lbox_top + 28.0f;

            dl->ChannelsSplit( 2 );
            dl->ChannelsSetCurrent( 1 );

            ImGui::SetCursorPos( ImVec2( L_X + 10.0f, ly ) );
            checkbox( "Enable autobot", &m_autobot.enabled );
            ly = ImGui::GetCursorPos( ).y;

            const float lbox_bottom = ly + 10.0f;
            dl->ChannelsSetCurrent( 0 );
            draw_card( dl, wpos, L_X, lbox_top, lbox_bottom, L_W, "autobot options", colors::col_hdr );
            dl->ChannelsMerge( );

            const float rbox_top = TITLE_H + 12.0f;
            float ry = rbox_top + 28.0f;

            dl->ChannelsSplit( 2 );
            dl->ChannelsSetCurrent( 1 );

            if ( m_autobot.enabled && snap.game.cur_state == osu::game_state_t::play ) {
                dl->AddText( S( R_X + 12.0f, ry ), IM_COL32( 100, 255, 100, 255 ), "Status: Running" );
            }
            else {
                dl->AddText( S( R_X + 12.0f, ry ), colors::text_dim, "Status: Idle" );
            }
            ry += ImGui::GetTextLineHeight( ) + 8.0f;

            const float rbox_bottom = ry + 10.0f;
            dl->ChannelsSetCurrent( 0 );
            draw_card( dl, wpos, R_X, rbox_top, rbox_bottom, R_W, "diagnostics", colors::col_hdr );
            dl->ChannelsMerge( );
        }
        else if ( m_tab == 4 ) {
            const float lbox_top = TITLE_H + 12.0f;
            float ly = lbox_top + 28.0f;

            dl->ChannelsSplit( 2 );
            dl->ChannelsSetCurrent( 1 );

            dl->AddText( S( L_X + 12.0f, ly ), colors::text_bright, "Custom Gameplay Keys:" );
            ly += ImGui::GetTextLineHeight( ) + 8.0f;

            char left_buf[ 16 ]{};
            char right_buf[ 16 ]{};
            GetKeyNameTextA( MapVirtualKeyA( m_custom_left_key, MAPVK_VK_TO_VSC ) << 16, left_buf, sizeof( left_buf ) );
            GetKeyNameTextA( MapVirtualKeyA( m_custom_right_key, MAPVK_VK_TO_VSC ) << 16, right_buf, sizeof( right_buf ) );
            if ( !left_buf[ 0 ] ) {
                if ( m_custom_left_key >= 32 && m_custom_left_key <= 126 ) {
                    sprintf_s( left_buf, "%c", m_custom_left_key );
                }
                else {
                    sprintf_s( left_buf, "0x%02X", m_custom_left_key );
                }
            }
            if ( !right_buf[ 0 ] ) {
                if ( m_custom_right_key >= 32 && m_custom_right_key <= 126 ) {
                    sprintf_s( right_buf, "%c", m_custom_right_key );
                }
                else {
                    sprintf_s( right_buf, "0x%02X", m_custom_right_key );
                }
            }

            if ( m_waiting_left ) {
                ImGui::SetCursorPos( ImVec2( L_X + 10.0f, ly ) );
                button( "Press key...##lbtn", L_W - 20.0f, 20.0f );
                for ( int k = 8; k < 256; ++k ) {
                    if ( k == VK_LBUTTON || k == VK_RBUTTON || k == VK_MBUTTON ) {
                        continue;
                    }
                    if ( GetAsyncKeyState( k ) & 0x8000 ) {
                        m_custom_left_key = k;
                        m_waiting_left = false;
                        break;
                    }
                }
            }
            else {
                const std::string lbl = std::string( "Left Key: " ) + left_buf + "##lbtn";
                ImGui::SetCursorPos( ImVec2( L_X + 10.0f, ly ) );
                if ( button( lbl.c_str( ), L_W - 20.0f, 20.0f ) ) {
                    m_waiting_left = true;
                    m_waiting_right = false;
                }
            }
            ly += 24.0f;

            if ( m_waiting_right ) {
                ImGui::SetCursorPos( ImVec2( L_X + 10.0f, ly ) );
                button( "Press key...##rbtn", L_W - 20.0f, 20.0f );
                for ( int k = 8; k < 256; ++k ) {
                    if ( k == VK_LBUTTON || k == VK_RBUTTON || k == VK_MBUTTON ) {
                        continue;
                    }
                    if ( GetAsyncKeyState( k ) & 0x8000 ) {
                        m_custom_right_key = k;
                        m_waiting_right = false;
                        break;
                    }
                }
            }
            else {
                const std::string lbl = std::string( "Right Key: " ) + right_buf + "##rbtn";
                ImGui::SetCursorPos( ImVec2( L_X + 10.0f, ly ) );
                if ( button( lbl.c_str( ), L_W - 20.0f, 20.0f ) ) {
                    m_waiting_right = true;
                    m_waiting_left = false;
                }
            }
            ly += 26.0f;

            ImGui::SetCursorPos( ImVec2( L_X + 10.0f, ly ) );
            checkbox( "Stream proof", &stream_proof );
            ly = ImGui::GetCursorPos( ).y + 4.0f;

            ImGui::SetCursorPos( ImVec2( L_X + 10.0f, ly ) );
            if ( checkbox( "Block scores", &score_blocker ) ) {
                if ( m_cache ) {
                    set_score_blocker_state( score_blocker, m_cache->process_handle( ) );
                }
            }
            ly = ImGui::GetCursorPos( ).y;

            const float lbox_bottom = ly + 10.0f;
            dl->ChannelsSetCurrent( 0 );
            draw_card( dl, wpos, L_X, lbox_top, lbox_bottom, L_W, "gameplay bindings", colors::col_hdr );
            dl->ChannelsMerge( );

            const float rbox_top = TITLE_H + 12.0f;
            float ry = rbox_top + 28.0f;

            dl->ChannelsSplit( 2 );
            dl->ChannelsSetCurrent( 1 );

            const char* client = "none";
            if ( snap.game.client == osu::client_kind_t::stable ) {
                client = "osu!stable";
            }
            else if ( snap.game.client == osu::client_kind_t::lazer ) {
                client = "osu!lazer";
            }

            const bool osu_wnd = input::target_window( ) && IsWindow( input::target_window( ) );

            char buf[ 128 ];
            sprintf_s( buf, "Osu window: %s", osu_wnd ? "found" : "not found" );
            dl->AddText( S( R_X + 12.0f, ry ), osu_wnd ? IM_COL32( 100, 255, 100, 255 ) : colors::text_dim, buf );
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            sprintf_s( buf, "Client: %s", client );
            dl->AddText( S( R_X + 12.0f, ry ), colors::text_bright, buf );
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            sprintf_s( buf, "Attached PID: %d", snap.game.pid );
            dl->AddText( S( R_X + 12.0f, ry ), colors::text, buf );
            ry += ImGui::GetTextLineHeight( ) + 4.0f;

            if ( snap.game.cur_state == osu::game_state_t::play ) {
                sprintf_s( buf, "Time: %d ms", snap.game.cur_time );
            }
            else {
                sprintf_s( buf, "Time: --" );
            }
            dl->AddText( S( R_X + 12.0f, ry ), colors::text, buf );
            ry += ImGui::GetTextLineHeight( ) + 8.0f;

            if ( snap.game.client == osu::client_kind_t::stable ) {
                dl->AddText( S( R_X + 12.0f, ry ), colors::text_dim, "Songs path override:" );
                ry += ImGui::GetTextLineHeight( ) + 4.0f;

                ImGui::SetCursorPos( ImVec2( R_X + 10.0f, ry ) );
                text_input( "##songs_override", m_songs_path_utf8, IM_ARRAYSIZE( m_songs_path_utf8 ), R_W - 20.0f );
                ry = ImGui::GetCursorPos( ).y + 6.0f;

                ImGui::SetCursorPos( ImVec2( R_X + 10.0f, ry ) );
                if ( button( "Browse##songs", R_W - 20.0f, 20.0f ) ) {
                    BROWSEINFOW bi{};
                    wchar_t buffer[ MAX_PATH ]{};
                    bi.lpszTitle = L"Select osu! Songs folder";
                    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
                    if ( PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW( &bi ) ) {
                        if ( SHGetPathFromIDListW( pidl, buffer ) ) {
                            WideCharToMultiByte( CP_UTF8, 0, buffer, -1, m_songs_path_utf8, IM_ARRAYSIZE( m_songs_path_utf8 ), nullptr, nullptr );
                        }
                        CoTaskMemFree( pidl );
                    }
                }
                ry = ImGui::GetCursorPos( ).y + 4.0f;

                ImGui::SetCursorPos( ImVec2( R_X + 10.0f, ry ) );
                if ( button( "Apply Override", R_W - 20.0f, 20.0f ) && m_cache && m_songs_path_utf8[ 0 ] ) {
                    wchar_t wide[ 512 ]{};
                    MultiByteToWideChar( CP_UTF8, 0, m_songs_path_utf8, -1, wide, 512 );
                    m_cache->stable_parser( ).set_songs_path( wide );
                }
                ry = ImGui::GetCursorPos( ).y;
            }

            const float rbox_bottom = ry + 10.0f;
            dl->ChannelsSetCurrent( 0 );
            draw_card( dl, wpos, R_X, rbox_top, rbox_bottom, R_W, "system diagnostic", colors::col_hdr );
            dl->ChannelsMerge( );
        }

        render_open_dropdown( );
        render_open_color_picker( );

        dl->AddRect( wpos, ImVec2( wpos.x + MENU_W, wpos.y + MENU_H ), IM_COL32( 160, 100, 255, 90 ), 12.0f, 0, 1.5f );

        ImGui::SetCursorPos( ImVec2( MENU_W, MENU_H ) );
        ImGui::Dummy( ImVec2( 1.0f, 1.0f ) );

        ImGui::End( );
    }
}