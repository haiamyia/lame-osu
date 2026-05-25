#pragma once

#include <impl/memory/process.hxx>
#include <impl/struct/game_snapshot.hxx>
#include <memory>

namespace game {

    class i_osu_client {
    public:
        virtual ~i_osu_client( ) = default;

        virtual bool attach( memory::c_process& process ) = 0;
        virtual void update( memory::c_process& process, osu::game_snapshot_t& snap ) = 0;
        virtual osu::client_kind_t kind( ) const = 0;
    };

    using osu_client_ptr = std::unique_ptr<i_osu_client>;

}