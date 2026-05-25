#pragma once

#include <impl/struct/game_snapshot.hxx>
#include <impl/memory/process.hxx>

namespace beatmap {

    class i_beatmap_provider {
    public:
        virtual ~i_beatmap_provider( ) = default;
        virtual bool try_load( memory::c_process& process, const osu::game_snapshot_t& game, osu::beatmap_data_t& out ) = 0;
    };

}