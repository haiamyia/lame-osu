#pragma once

#include <core/game/i_osu_client.hxx>
#include <impl/defs/offsets_lazer.hxx>
#include <impl/memory/scanner.hxx>

namespace game {

    class c_osu_lazer : public i_osu_client {
    public:
        explicit c_osu_lazer( offsets::lazer::table_t offsets ) : m_off( offsets ) {}

        bool attach( memory::c_process& process ) override {
            memory::c_scanner scanner( process );
            const auto pattern_addr = scanner.scan( offsets::lazer::anchor_pattern, 0 );
            if ( !pattern_addr )
                return false;

            const auto external_link_opener = process.read<uint64_t>( pattern_addr - 0x24 );
            if ( !external_link_opener )
                return false;

            const auto api = process.read<uint64_t>( external_link_opener + m_off.ext_link_opener_api );
            if ( !api )
                return false;

            m_game_base = process.read<uint64_t>( api + m_off.api_access_game );
            return m_game_base != 0;
        }

        void update( memory::c_process& process, osu::game_snapshot_t& snap ) override {
            snap.client = osu::client_kind_t::lazer;
            snap.game_base = m_game_base;
            snap.offset_version = m_off.osu_version;
            snap.player_screen = 0;
            snap.drawable_ruleset = 0;

            if ( !m_game_base )
                return;

            const auto beatmap_clock = process.read<uint64_t>( m_game_base + m_off.game_base_beatmap_clock );
            if ( beatmap_clock ) {
                const auto final_source = process.read<uint64_t>( beatmap_clock + m_off.framed_clock_final_source );
                if ( final_source ) {
                    const auto raw_time = process.read<double>( final_source + m_off.framed_clock_current_time );
                    snap.cur_time = static_cast<int32_t>( raw_time );
                }
            }

            const auto beatmap_bindable = process.read<uint64_t>( m_game_base + m_off.game_base_beatmap );
            if ( beatmap_bindable ) {
                const auto working_beatmap = process.read<uint64_t>( beatmap_bindable + m_off.bindable_value );
                if ( working_beatmap ) {
                    const auto beatmap_info = process.read<uint64_t>( working_beatmap + m_off.working_map_info );
                    const auto set_info = process.read<uint64_t>( working_beatmap + m_off.working_map_set_info );
                    if ( beatmap_info ) {
                        snap.map_id = process.read<int32_t>( beatmap_info + m_off.map_info_online_id );
                        process.read_dotnet_string( beatmap_info + m_off.map_info_hash, snap.beatmap_hash );
                        process.read_dotnet_string( beatmap_info + m_off.map_info_difficulty, snap.beatmap_version );
                    }
                    if ( set_info )
                        snap.set_id = process.read<int32_t>( set_info + m_off.set_info_online_id );
                }
            }

            const auto screen_stack = process.read<uint64_t>( m_game_base + m_off.game_screen_stack );
            const auto stack = screen_stack ? process.read<uint64_t>( screen_stack + m_off.screen_stack_stack ) : 0;
            const auto stack_count = stack ? process.read<int32_t>( stack + 0x10 ) : 0;

            if ( !screen_stack || !stack || stack_count <= 0 )
                return;

            const auto items = process.read<uint64_t>( stack + 0x8 );
            const auto current_screen = process.read<uint64_t>( items + m_off.array_first_element + 0x8 * static_cast<uint64_t>( stack_count - 1 ) );
            snap.player_screen = current_screen;

            const auto base_api = process.read<uint64_t>( m_game_base + m_off.game_base_api );
            auto screen_api = process.read<uint64_t>( current_screen + m_off.submitting_player_api );
            if ( !screen_api || screen_api != base_api )
                screen_api = process.read<uint64_t>( current_screen + m_off.player_api );

            const auto drawable_ruleset = process.read<uint64_t>( current_screen + m_off.player_drawable_ruleset );

            if ( screen_api != 0 && screen_api == base_api && drawable_ruleset != 0 ) {
                snap.cur_state = osu::game_state_t::play;
                snap.drawable_ruleset = drawable_ruleset;
            }
            else {
                snap.cur_state = osu::game_state_t::main_menu;
            }
        }

        osu::client_kind_t kind( ) const override { return osu::client_kind_t::lazer; }

        [[nodiscard]] const offsets::lazer::table_t& offsets( ) const { return m_off; }

    private:
        offsets::lazer::table_t m_off;
        uint64_t m_game_base = 0;
    };

}