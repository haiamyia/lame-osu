#pragma once

#include <impl/deps/lzma_helper.hpp>
#include <vector>
#include <string>
#include <cstdint>

namespace lzma {

    inline bool decode_osr_frames( const std::vector<uint8_t>& in, std::string& out ) {
        if ( in.empty( ) )
            return false;

        std::vector<lh::Byte> blob( in.begin( ), in.end( ) );
        const auto decoded = lh::lzma_decompress( blob );
        if ( decoded.empty( ) )
            return false;

        out.assign( reinterpret_cast<const char*>( decoded.data( ) ), decoded.size( ) );
        return out.find( '|' ) != std::string::npos;
    }

}