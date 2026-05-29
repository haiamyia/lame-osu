#pragma once

#include <cstdint>

namespace offsets::stable {

    inline constexpr const char* pattern_time = "a1 ? ? ? ? a3 ? ? ? ? 83 C4 38 5e 5f";
    inline constexpr int32_t pattern_time_offset = 1;

    inline constexpr const char* pattern_state = "A1 ? ? ? ? A3 ? ? ? ? 83 3D ? ? ? ? ? ? ? ? ? ? ? B9 ? ? ? ? E8";
    inline constexpr int32_t pattern_state_offset = 6;

    inline constexpr const char* pattern_beatmap = "F8 01 74 04 83 65";
    inline constexpr int32_t pattern_beatmap_offset = -0xC;

    inline constexpr uint32_t beatmap_map_id = 0xC8;
    inline constexpr uint32_t beatmap_set_id = 0xCC;
    inline constexpr uint32_t beatmap_folder_name = 0x74;
    inline constexpr uint32_t beatmap_file_name = 0x94;

    inline constexpr const char* pattern_menu_mods = "81 0d ? ? ? ? 00 08 00 00 c7";
    inline constexpr int32_t pattern_menu_mods_offset = 2;

}