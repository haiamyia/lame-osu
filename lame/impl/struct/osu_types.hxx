#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace osu {

    enum class client_kind_t : uint8_t {
        none = 0,
        stable,
        lazer
    };

    enum class game_state_t : int32_t {
        main_menu = 0,
        edit = 1,
        play = 2,
        exit = 3,
        select_edit = 4,
        select_play = 5,
        unknown = 12
    };

    enum hit_object_type_t : uint8_t {
        circle = 1,
        slider = 2,
        spinner = 8
    };

    struct hit_object_t {
        int32_t start_time = 0;
        int32_t end_time = 0;
        float x = 0.f;
        float y = 0.f;
        float screen_x = 0.f;
        float screen_y = 0.f;
        uint8_t type = 0;
        int32_t stack_index = 0;
    };

    struct beatmap_data_t {
        int32_t map_id = 0;
        int32_t set_id = 0;
        float cs = 5.f;
        int32_t screen_width = 1920;
        int32_t screen_height = 1080;
        std::vector<hit_object_t> objects;
        bool loaded = false;
        std::string error;
        std::string songs_path;
        std::string beatmap_path;
    };

}