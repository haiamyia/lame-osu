#pragma once

#include <impl/struct/osu_types.hxx>
#include <string>
#include <cstdint>

namespace osu {

    struct game_snapshot_t {
        bool attached = false;
        client_kind_t client = client_kind_t::none;
        int32_t pid = 0;
        int32_t cur_time = 0;
        game_state_t cur_state = game_state_t::main_menu;
        int32_t cur_mod_state = 0;
        int32_t map_id = 0;
        int32_t set_id = 0;
        std::string map_folder;
        std::string map_file;
        std::string beatmap_hash;
        std::string beatmap_version;
        uint64_t game_base = 0;
        uint64_t player_screen = 0;
        uint64_t drawable_ruleset = 0;
        std::string client_version;
        std::string offset_version;
        bool offset_mismatch = false;
        std::string songs_path;
        int32_t left_key = 'Z';
        int32_t right_key = 'X';
    };

    struct full_snapshot_t {
        game_snapshot_t game;
        beatmap_data_t beatmap;
    };

}