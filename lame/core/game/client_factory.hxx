#pragma once

#include <core/game/osu_lazer.hxx>
#include <core/game/osu_stable.hxx>
#include <impl/memory/process.hxx>
#include <impl/defs/offsets_lazer.hxx>

namespace game {

    inline osu_client_ptr try_attach( memory::c_process& process, const offsets::lazer::table_t& lazer_offsets ) {
        if ( !process.is_32bit( ) ) {
            auto lazer = std::make_unique<c_osu_lazer>( lazer_offsets );
            if ( lazer->attach( process ) )
                return lazer;
        }

        auto stable = std::make_unique<c_osu_stable>( );
        if ( stable->attach( process ) )
            return stable;

        if ( !process.is_32bit( ) ) {
            auto lazer = std::make_unique<c_osu_lazer>( lazer_offsets );
            if ( lazer->attach( process ) )
                return lazer;
        }

        return nullptr;
    }

}