#pragma once

#include <core/game/i_osu_client.hxx>
#include <impl/defs/offsets_stable.hxx>
#include <impl/memory/scanner.hxx>

namespace game {

    class c_osu_stable : public i_osu_client {
    public:
        bool attach( memory::c_process& process ) override {
            memory::c_scanner scanner( process );

            const auto time_scan = scanner.scan( offsets::stable::pattern_time, offsets::stable::pattern_time_offset );
            const auto state_scan = scanner.scan( offsets::stable::pattern_state, offsets::stable::pattern_state_offset );
            const auto beatmap_scan = scanner.scan( offsets::stable::pattern_beatmap, offsets::stable::pattern_beatmap_offset );
            const auto left_key_scan = scanner.scan( offsets::stable::pattern_left_key, offsets::stable::pattern_left_key_offset );
            const auto right_key_scan = scanner.scan( offsets::stable::pattern_right_key, offsets::stable::pattern_right_key_offset );
            const auto mods_scan = scanner.scan( offsets::stable::pattern_menu_mods, offsets::stable::pattern_menu_mods_offset );

            if ( !time_scan || !state_scan )
                return false;

            m_time_ptr = process.read<uint32_t>( time_scan );
            m_state_ptr = process.read<uint32_t>( state_scan );
            if ( beatmap_scan )
                m_beatmap_base = process.read<uint32_t>( beatmap_scan );
            if ( left_key_scan )
                m_left_key_addr = left_key_scan;
            if ( right_key_scan )
                m_right_key_addr = right_key_scan;
            if ( mods_scan )
                m_mods_ptr = process.read<uint32_t>( mods_scan );

            m_logged_first_play = false;
            m_logged_key_fallback = false;

            return m_time_ptr != 0 && m_state_ptr != 0;
        }

        void update( memory::c_process& process, osu::game_snapshot_t& snap ) override {
            snap.client = osu::client_kind_t::stable;
            snap.game_base = 0;

            if ( !m_time_ptr || !m_state_ptr )
                return;

            snap.cur_time = process.read<int32_t>( m_time_ptr );
            snap.cur_state = static_cast<osu::game_state_t>( process.read<int32_t>( m_state_ptr ) );

            if ( m_mods_ptr )
                snap.cur_mod_state = process.read<int32_t>( m_mods_ptr );

            if ( m_beatmap_base ) {
                const auto beatmap = process.read<uint32_t>( m_beatmap_base );
                if ( beatmap ) {
                    snap.map_id = process.read<int32_t>( beatmap + offsets::stable::beatmap_map_id );
                    snap.set_id = process.read<int32_t>( beatmap + offsets::stable::beatmap_set_id );
                    process.read_dotnet_string( beatmap + offsets::stable::beatmap_folder_name, snap.map_folder );
                    process.read_dotnet_string( beatmap + offsets::stable::beatmap_file_name, snap.map_file );
                }
            }

            snap.left_key = 'Z';
            snap.right_key = 'X';

            if ( m_left_key_addr ) {
                const auto key = process.read<uint8_t>( m_left_key_addr );
                if ( key != 0 )
                    snap.left_key = key;
            }
            if ( m_right_key_addr ) {
                const auto key = process.read<uint8_t>( m_right_key_addr );
                if ( key != 0 )
                    snap.right_key = key;
            }

            if ( !m_logged_key_fallback &&
                 ( snap.left_key == 'Z' && snap.right_key == 'X' ) &&
                 ( !m_left_key_addr || !m_right_key_addr ) ) {
                m_logged_key_fallback = true;
            }

            if ( snap.cur_state == osu::game_state_t::play && !m_logged_first_play ) {
                m_logged_first_play = true;
            }

            if ( snap.cur_state != osu::game_state_t::play )
                m_logged_first_play = false;
        }

        osu::client_kind_t kind( ) const override { return osu::client_kind_t::stable; }

    private:
        uint64_t m_time_ptr = 0;
        uint64_t m_state_ptr = 0;
        uint64_t m_beatmap_base = 0;
        uint64_t m_left_key_addr = 0;
        uint64_t m_right_key_addr = 0;
        uint64_t m_mods_ptr = 0;
        bool m_logged_first_play = false;
        bool m_logged_key_fallback = false;
    };

}