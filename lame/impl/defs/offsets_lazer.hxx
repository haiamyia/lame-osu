#pragma once

#include <cstdint>
#include <string>

namespace offsets::lazer {

    inline constexpr const char* anchor_pattern =
        "00 00 80 44 00 00 40 44 00 00 00 00 ? ? ? ? 00 00 00 00";

    struct table_t {
        std::string osu_version = "2026.518.0";

        uint32_t api_access_game = 784;                  // APIAccess.game
        uint32_t ext_link_opener_api = 536;              // ExternalLinkOpener.<api>k__BackingField
        uint32_t game_base_beatmap_clock = 1232;         // OsuGameBase.beatmapClock
        uint32_t game_base_beatmap = 1104;               // OsuGameBase.<Beatmap>k__BackingField
        uint32_t game_base_api = 1080;                   // OsuGameBase.<API>k__BackingField
        uint32_t game_screen_stack = 1536;               // OsuGame.<ScreenStack>k__BackingField
        uint32_t screen_stack_stack = 800;               // ScreenStack.stack
        uint32_t framed_clock_final_source = 528;        // FramedBeatmapClock.finalClockSource
        uint32_t framed_clock_current_time = 48;         // FramedClock.<CurrentTime>k__BackingField
        uint32_t working_map_info = 8;                   // WorkingBeatmap.BeatmapInfo
        uint32_t working_map_set_info = 16;              // WorkingBeatmap.BeatmapSetInfo
        uint32_t map_info_online_id = 140;               // BeatmapInfo.<OnlineID>k__BackingField
        uint32_t map_info_hash = 80;                     // BeatmapInfo.<Hash>k__BackingField
        uint32_t map_info_difficulty = 24;               // BeatmapInfo.<DifficultyName>k__BackingField
        uint32_t set_info_online_id = 48;                // BeatmapSetInfo.<OnlineID>k__BackingField
        uint32_t submitting_player_api = 1264;           // SubmittingPlayer.<api>k__BackingField
        uint32_t player_api = 1008;                      // Player.<api>k__BackingField
        uint32_t player_drawable_ruleset = 1112;         // Player.<DrawableRuleset>k__BackingField
        uint32_t bindable_value = 0x20;                  // Bindable.Value
        uint32_t drawable_osu_beatmap = 848;             // DrawableOsuRuleset.Beatmap
        uint32_t beatmap_hit_objects = 48;               // Beatmap.<HitObjects>k__BackingField
        uint32_t list_items = 8;                         // List._items
        uint32_t list_size = 16;                         // List._size
        uint32_t array_first_element = 0x10;             // SZArray first element (.NET Core)
        uint32_t hit_object_start_time_bindable = 16;    // HitObject.StartTimeBindable
        uint32_t osu_hit_object_position = 0;
        uint32_t hit_object_property_bindable = 0;
        uint32_t slider_end_time = 0;

        [[nodiscard]] bool has_hitobject_offsets( ) const {
            return drawable_osu_beatmap != 0 && beatmap_hit_objects != 0 &&
                list_items != 0 && list_size != 0 && hit_object_start_time_bindable != 0 &&
                bindable_value != 0;
        }
    };

    inline bool load_from_file( table_t& out, const std::wstring& path ) {
        return true;
    }

    inline std::wstring default_json_path( ) {
        return L"";
    }

}