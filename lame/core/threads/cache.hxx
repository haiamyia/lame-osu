#pragma once

#include <core/game/client_factory.hxx>
#include <core/beatmap/stable_parser.hxx>
#include <core/beatmap/lazer_memory.hxx>
#include <core/beatmap/songs_resolver.hxx>
#include <impl/memory/ntdll.hxx>
#include <impl/memory/syscall.hxx>
#include <impl/memory/process.hxx>
#include <impl/defs/offsets_lazer.hxx>
#include <impl/util/playfield.hxx>
#include <impl/memory/input.hxx>
#include <shared_mutex>
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>

namespace threads {

    class c_cache {
    public:
        c_cache( ) = default;

        bool init( ) {
            const bool ntdll_ok = m_ntdll.load( );
            bool syscall_ok = false;
            if ( ntdll_ok )
                syscall_ok = m_syscall.setup( m_ntdll );

            if ( syscall_ok )
                m_process.set_syscall( m_syscall );
            else
                m_process.enable_fallback( );

            m_lazer_memory.set_offsets( &m_lazer_offsets );
            input::init( );
            return true;
        }

        void start( ) {
            if ( m_running.exchange( true ) )
                return;

            m_process_thread = std::thread( [ this ] { process_loop( ); } );
            m_game_thread = std::thread( [ this ] {
                ::SetThreadPriority( ::GetCurrentThread( ), THREAD_PRIORITY_TIME_CRITICAL );
                game_loop( );
            } );
            m_beatmap_thread = std::thread( [ this ] { beatmap_loop( ); } );
        }

        void stop( ) {
            m_running = false;
            if ( m_process_thread.joinable( ) )
                m_process_thread.join( );
            if ( m_game_thread.joinable( ) )
                m_game_thread.join( );
            if ( m_beatmap_thread.joinable( ) )
                m_beatmap_thread.join( );
        }

        osu::full_snapshot_t get_snapshot( ) const {
            std::shared_lock lock( m_mutex );
            return m_snapshot;
        }

        beatmap::c_stable_parser& stable_parser( ) { return m_stable_parser; }

        using module_tick_fn = std::function<void( const osu::full_snapshot_t& )>;

        void set_module_tick( module_tick_fn fn ) {
            std::lock_guard lock( m_module_tick_mutex );
            m_module_tick = std::move( fn );
        }

    private:
        memory::c_clean_ntdll m_ntdll;
        memory::c_syscall m_syscall;
        memory::c_process m_process;
        offsets::lazer::table_t m_lazer_offsets;
        game::osu_client_ptr m_client;

        beatmap::c_stable_parser m_stable_parser;
        beatmap::c_lazer_memory m_lazer_memory;

        mutable std::shared_mutex m_mutex;
        osu::full_snapshot_t m_snapshot;

        std::atomic<bool> m_running{ false };
        std::thread m_process_thread;
        std::thread m_game_thread;
        std::thread m_beatmap_thread;

        std::string m_last_map_sig;
        bool m_was_in_play = false;
        int32_t m_last_beatmap_game_time = -1;

        module_tick_fn m_module_tick;
        std::mutex m_module_tick_mutex;

        void try_attach_osu( ) {
            const auto pid = memory::c_process::find_pid_by_name( L"osu!.exe" );
            if ( !pid )
                return;

            if ( !m_process.attach( pid ) ) {
                return;
            }

            m_client = game::try_attach( m_process, m_lazer_offsets );
            if ( !m_client ) {
                return;
            }


            const auto osu_hwnd = playfield::find_osu_window( static_cast<DWORD>( pid ) );
            input::set_target_window( osu_hwnd );

            if ( m_client->kind( ) == osu::client_kind_t::stable ) {
                const auto songs = beatmap::songs::resolve( m_process );
                m_stable_parser.set_songs_path( songs );
                m_last_map_sig.clear( );

                {
                    std::unique_lock lock( m_mutex );
                    char narrow[ 512 ]{};
                    WideCharToMultiByte( CP_UTF8, 0, songs.c_str( ), -1, narrow, 512, nullptr, nullptr );
                    m_snapshot.game.songs_path = narrow;
                }
            }
            else if ( m_client->kind( ) == osu::client_kind_t::lazer ) {
                const auto data_dir = beatmap::songs::resolve_lazer_data_dir( m_process );
                m_lazer_memory.set_data_dir( data_dir );
                m_last_map_sig.clear( );

                {
                    std::unique_lock lock( m_mutex );
                    char narrow[ 512 ]{};
                    WideCharToMultiByte( CP_UTF8, 0, data_dir.c_str( ), -1, narrow, 512, nullptr, nullptr );
                    m_snapshot.game.songs_path = narrow;
                }

                std::thread( [ this ] { m_lazer_memory.warm_index( ); } ).detach( );
            }
        }

        void process_loop( ) {
            using namespace std::chrono_literals;
            while ( m_running ) {
                if ( !m_process.valid( ) ) {
                    try_attach_osu( );
                }
                else if ( !input::target_window( ) || !IsWindow( input::target_window( ) ) ) {
                    const auto hwnd = playfield::find_osu_window( static_cast<DWORD>( m_process.pid( ) ) );
                    if ( hwnd )
                        input::set_target_window( hwnd );
                }

                {
                    std::unique_lock lock( m_mutex );
                    m_snapshot.game.attached = m_process.valid( ) && m_client != nullptr;
                    m_snapshot.game.pid = m_process.pid( );
                    m_snapshot.game.offset_version = m_lazer_offsets.osu_version;
                }

                std::this_thread::sleep_for( 5ms );
            }
        }

        void game_loop( ) {
            using namespace std::chrono_literals;
            while ( m_running ) {
                bool in_play = false;
                if ( m_client && m_process.valid( ) ) {
                    osu::game_snapshot_t snap;
                    snap.attached = true;
                    snap.pid = m_process.pid( );
                    m_client->update( m_process, snap );
                    in_play = snap.cur_state == osu::game_state_t::play;

                    osu::full_snapshot_t full{};
                    {
                        std::unique_lock lock( m_mutex );
                        if ( snap.songs_path.empty( ) )
                            snap.songs_path = m_snapshot.game.songs_path;
                        m_snapshot.game = snap;
                        full.game = m_snapshot.game;
                        full.beatmap = m_snapshot.beatmap;
                    }

                    module_tick_fn tick;
                    {
                        std::lock_guard lock( m_module_tick_mutex );
                        tick = m_module_tick;
                    }
                    if ( tick )
                        tick( full );
                }

                if ( in_play )
                    std::this_thread::yield( );
                else
                    std::this_thread::sleep_for( 1ms );
            }
        }

        void beatmap_loop( ) {
            using namespace std::chrono_literals;
            while ( m_running ) {
                osu::game_snapshot_t game_snap;
                {
                    std::shared_lock lock( m_mutex );
                    game_snap = m_snapshot.game;
                }

                const bool in_play = game_snap.cur_state == osu::game_state_t::play;
                const bool in_select_play = game_snap.cur_state == osu::game_state_t::select_play;
                const bool load_state = in_play || in_select_play;

                if ( m_was_in_play && !in_play ) {
                    std::unique_lock lock( m_mutex );
                    m_snapshot.beatmap = {};
                    m_last_map_sig.clear( );
                    m_last_beatmap_game_time = -1;
                }
                m_was_in_play = in_play;

                if ( in_play && game_snap.cur_time < m_last_beatmap_game_time - 200 )
                    m_last_map_sig.clear( );
                if ( in_play )
                    m_last_beatmap_game_time = game_snap.cur_time;

                bool beatmap_loaded = false;
                {
                    std::shared_lock lock( m_mutex );
                    beatmap_loaded = m_snapshot.beatmap.loaded;
                }

                const bool has_map =
                    game_snap.map_id != 0 ||
                    !game_snap.beatmap_hash.empty( ) ||
                    ( game_snap.set_id > 0 && !game_snap.beatmap_version.empty( ) ) ||
                    ( !game_snap.map_folder.empty( ) && !game_snap.map_file.empty( ) );
                const std::string map_sig =
                    std::to_string( game_snap.map_id ) + "|" + std::to_string( game_snap.set_id ) + "|" +
                    game_snap.map_folder + "|" + game_snap.map_file + "|" + game_snap.beatmap_hash + "|" +
                    game_snap.beatmap_version;
                const bool map_changed = has_map && map_sig != m_last_map_sig;
                const bool retry_reload =
                    in_play && has_map && !beatmap_loaded &&
                    ( game_snap.cur_time < 2000 || map_changed );

                if ( game_snap.attached && load_state && has_map && ( map_changed || retry_reload ) ) {

                    if ( map_changed ) {
                        m_lazer_memory.invalidate_cache( );
                        {
                            std::unique_lock lock( m_mutex );
                            m_snapshot.beatmap = {};
                        }
                    }

                    osu::beatmap_data_t beatmap;
                    bool ok = false;

                    if ( game_snap.client == osu::client_kind_t::stable )
                        ok = m_stable_parser.try_load( m_process, game_snap, beatmap );
                    else if ( game_snap.client == osu::client_kind_t::lazer )
                        ok = m_lazer_memory.try_load( m_process, game_snap, beatmap );

                    if ( ok ) {
                        char narrow[ 512 ]{};
                        const auto& path =
                            game_snap.client == osu::client_kind_t::lazer
                                ? m_lazer_memory.last_beatmap_path( )
                                : m_stable_parser.last_beatmap_path( );
                        WideCharToMultiByte( CP_UTF8, 0, path.c_str( ), -1, narrow, 512, nullptr, nullptr );
                    }

                    {
                        std::unique_lock lock( m_mutex );
                        if ( ok )
                            m_snapshot.beatmap = beatmap;
                        if ( ok )
                            m_last_map_sig = map_sig;
                    }
                }

                std::this_thread::sleep_for( 10ms );
            }
        }
    };

}