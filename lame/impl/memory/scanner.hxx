#pragma once

#include <impl/memory/process.hxx>
#include <vector>
#include <string>
#include <sstream>
#include <cstdint>

namespace memory {

    class c_scanner {
    public:
        struct region_t {
            uint64_t base = 0;
            size_t size = 0;
        };

        explicit c_scanner( const c_process& process ) : m_process( &process ) {
            enumerate_regions( );
        }

        uint64_t scan( const char* ida_pattern, int32_t result_offset = 0 ) const {
            std::vector<uint16_t> pattern;
            std::stringstream ss( ida_pattern );
            std::string token;
            while ( ss >> token ) {
                if ( token == "?" )
                    pattern.push_back( 0xFFFF );
                else
                    pattern.push_back( static_cast<uint16_t>( std::stoul( token, nullptr, 16 ) ) );
            }

            if ( pattern.empty( ) )
                return 0;

            for ( const auto& region : m_regions ) {
                std::vector<uint8_t> buffer( region.size );
                if ( !m_process->read_buffer( region.base, buffer.data( ), region.size ) )
                    continue;

                const auto limit = buffer.size() - pattern.size();
                for ( size_t i = 0; i < limit; ++i ) {
                    bool found = true;
                    for ( size_t j = 0; j < pattern.size( ); ++j ) {
                        if ( pattern[ j ] != 0xFFFF && pattern[ j ] != buffer[ i + j ] ) {
                            found = false;
                            break;
                        }
                    }
                    if ( found )
                        return region.base + i + result_offset;
                }
            }

            return 0;
        }

    private:
        const c_process* m_process = nullptr;
        std::vector<region_t> m_regions;

        void enumerate_regions( ) {
            if ( !m_process || !m_process->valid( ) )
                return;

            MEMORY_BASIC_INFORMATION mbi{};
            uint64_t address = 0;

            while ( VirtualQueryEx( m_process->handle( ), reinterpret_cast<LPCVOID>( address ), &mbi, sizeof( mbi ) ) ) {
                if ( mbi.State == MEM_COMMIT &&
                     ( mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_EXECUTE_READWRITE ||
                       mbi.Protect == PAGE_READONLY || mbi.Protect == PAGE_EXECUTE_READ ) ) {
                    m_regions.push_back( {
                        reinterpret_cast<uint64_t>( mbi.BaseAddress ),
                        mbi.RegionSize
                    } );
                }
                address = reinterpret_cast<uint64_t>( mbi.BaseAddress ) + mbi.RegionSize;
            }
        }
    };

}