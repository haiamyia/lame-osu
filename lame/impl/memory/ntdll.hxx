#pragma once

#include <Windows.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace memory {

    class c_clean_ntdll {
    public:
        bool load( const wchar_t* path = L"C:\\Windows\\System32\\ntdll.dll" ) {
            m_image.clear( );
            m_syscalls.clear( );

            const auto handle = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr );
            if ( handle == INVALID_HANDLE_VALUE ) {
                return false;
            }

            const auto size = GetFileSize( handle, nullptr );
            if ( size == INVALID_FILE_SIZE || size < sizeof( IMAGE_DOS_HEADER ) ) {
                CloseHandle( handle );
                return false;
            }

            m_image.resize( size );
            DWORD read = 0;
            const auto ok = ReadFile( handle, m_image.data( ), size, &read, nullptr ) && read == size;
            CloseHandle( handle );
            if ( !ok ) {
                return false;
            }

            if ( !parse_exports( ) ) {
                return false;
            }

            return true;
        }

        [[nodiscard]] uint32_t syscall_index( const char* name ) const {
            const auto it = m_syscalls.find( name );
            return it != m_syscalls.end( ) ? it->second : 0;
        }

        [[nodiscard]] const std::vector<uint8_t>& image( ) const { return m_image; }

    private:
        std::vector<uint8_t> m_image;
        std::unordered_map<std::string, uint32_t> m_syscalls;

        const uint8_t* rva_to_ptr( uint32_t rva, const IMAGE_NT_HEADERS64* nt ) const {
            const auto* section = IMAGE_FIRST_SECTION( nt );
            for ( WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section ) {
                if ( rva >= section->VirtualAddress &&
                     rva < section->VirtualAddress + section->Misc.VirtualSize ) {
                    return m_image.data( ) + section->PointerToRawData + ( rva - section->VirtualAddress );
                }
            }
            return nullptr;
        }

        const uint8_t* resolve_syscall_stub( const uint8_t* func ) const {
            if ( !func )
                return nullptr;

            for ( int hop = 0; hop < 8; ++hop ) {
                if ( func[ 0 ] == 0x4C && func[ 1 ] == 0x8B && func[ 2 ] == 0xD1 && func[ 3 ] == 0xB8 )
                    return func;

                if ( func[ 0 ] == 0xE9 ) {
                    const auto rel = *reinterpret_cast<const int32_t*>( func + 1 );
                    func = func + 5 + rel;
                    continue;
                }

                if ( func[ 0 ] == 0xEB ) {
                    const auto rel = *reinterpret_cast<const int8_t*>( func + 1 );
                    func = func + 2 + rel;
                    continue;
                }

                break;
            }

            return nullptr;
        }

        bool parse_exports( ) {
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>( m_image.data( ) );
            if ( dos->e_magic != IMAGE_DOS_SIGNATURE )
                return false;

            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>( m_image.data( ) + dos->e_lfanew );
            if ( nt->Signature != IMAGE_NT_SIGNATURE )
                return false;

            const auto& dir = nt->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_EXPORT ];
            if ( !dir.VirtualAddress || !dir.Size )
                return false;

            const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>( rva_to_ptr( dir.VirtualAddress, nt ) );
            if ( !exports )
                return false;

            const auto* names = reinterpret_cast<const uint32_t*>( rva_to_ptr( exports->AddressOfNames, nt ) );
            const auto* ordinals = reinterpret_cast<const uint16_t*>( rva_to_ptr( exports->AddressOfNameOrdinals, nt ) );
            const auto* functions = reinterpret_cast<const uint32_t*>( rva_to_ptr( exports->AddressOfFunctions, nt ) );
            if ( !names || !ordinals || !functions )
                return false;

            for ( DWORD i = 0; i < exports->NumberOfNames; ++i ) {
                const auto* export_name = reinterpret_cast<const char*>( rva_to_ptr( names[ i ], nt ) );
                if ( !export_name || export_name[ 0 ] != 'N' || export_name[ 1 ] != 't' )
                    continue;

                const auto ordinal = ordinals[ i ];
                const auto func_rva = functions[ ordinal ];
                const auto* func = rva_to_ptr( func_rva, nt );
                const auto* stub = resolve_syscall_stub( func );
                if ( !stub )
                    continue;

                const auto id = *reinterpret_cast<const uint32_t*>( stub + 4 );
                m_syscalls[ export_name ] = id;
            }

            return !m_syscalls.empty( );
        }
    };

}