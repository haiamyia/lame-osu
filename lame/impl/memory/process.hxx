#pragma once

#include <impl/memory/syscall.hxx>
#include <Windows.h>
#include <TlHelp32.h>
#include <cstdint>
#include <string>
#include <vector>

namespace memory {

    class c_process {
    public:
        c_process( ) = default;

        void set_syscall( const c_syscall& syscall ) {
            m_syscall = &syscall;
            m_use_fallback = false;
        }

        void enable_fallback( ) {
            m_syscall = nullptr;
            m_use_fallback = true;
        }

        [[nodiscard]] bool uses_fallback( ) const { return m_use_fallback; }

        bool attach( int32_t pid ) {
            detach( );
            if ( !pid )
                return false;

            if ( m_use_fallback ) {
                m_handle = OpenProcess( PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, static_cast<DWORD>( pid ) );
                if ( !m_handle )
                    return false;

                m_pid = pid;
                BOOL wow64 = FALSE;
                IsWow64Process( m_handle, &wow64 );
                m_is_32bit = wow64 != FALSE;
                return true;
            }

            if ( !m_syscall )
                return false;

            CLIENT_ID cid{};
            cid.UniqueProcess = reinterpret_cast<HANDLE>( static_cast<uintptr_t>( pid ) );

            OBJECT_ATTRIBUTES oa{};
            InitializeObjectAttributes( &oa, nullptr, 0, nullptr, nullptr );

            HANDLE handle = nullptr;
            const auto status = m_syscall->nt_open_process(
                &handle,
                PROCESS_VM_READ | PROCESS_QUERY_INFORMATION,
                &oa,
                &cid );

            if ( status < 0 || !handle )
                return false;

            m_handle = handle;
            m_pid = pid;

            BOOL wow64 = FALSE;
            IsWow64Process( handle, &wow64 );
            m_is_32bit = wow64 != FALSE;

            return true;
        }

        void detach( ) {
            if ( m_handle ) {
                CloseHandle( m_handle );
                m_handle = nullptr;
            }
            m_pid = 0;
            m_is_32bit = false;
        }

        [[nodiscard]] bool valid( ) const { return m_handle != nullptr; }
        [[nodiscard]] HANDLE handle( ) const { return m_handle; }
        [[nodiscard]] int32_t pid( ) const { return m_pid; }
        [[nodiscard]] bool is_32bit( ) const { return m_is_32bit; }

        bool read_buffer( uint64_t address, void* buffer, size_t size ) const {
            if ( !m_handle || !buffer || !size )
                return false;

            if ( m_use_fallback ) {
                SIZE_T read = 0;
                return ReadProcessMemory(
                    m_handle,
                    reinterpret_cast<LPCVOID>( address ),
                    buffer,
                    size,
                    &read ) != FALSE && read == size;
            }

            SIZE_T read = 0;
            const auto status = m_syscall->nt_read_virtual_memory(
                m_handle,
                reinterpret_cast<void*>( address ),
                buffer,
                size,
                &read );

            return status >= 0 && read == size;
        }

        template<typename T>
        T read( uint64_t address ) const {
            T value{};
            read_buffer( address, &value, sizeof( T ) );
            return value;
        }

        template<typename T>
        bool read( uint64_t address, T& out ) const {
            return read_buffer( address, &out, sizeof( T ) );
        }

        bool read_dotnet_string_object( uint64_t string_obj, std::string& out ) const {
            out.clear( );
            if ( !string_obj )
                return false;

            struct layout_t {
                int32_t length_offset = 0;
                int32_t chars_offset = 0;
            };

            const layout_t layouts[] = {
                { m_is_32bit ? 0x4 : 0x10, m_is_32bit ? 0x8 : 0x14 },
                { 0x4, 0x8 },
                { 0x8, 0xC },
            };

            for ( const auto& layout : layouts ) {
                const int32_t length = read<int32_t>( string_obj + layout.length_offset );
                if ( length <= 0 || length > 512 )
                    continue;

                std::vector<wchar_t> buffer( static_cast<size_t>( length ) + 1 );
                if ( !read_buffer(
                         string_obj + layout.chars_offset,
                         buffer.data( ),
                         static_cast<size_t>( length ) * sizeof( wchar_t ) ) )
                    continue;

                buffer[ static_cast<size_t>( length ) ] = L'\0';

                const int len = WideCharToMultiByte(
                    CP_UTF8, 0, buffer.data( ), length, nullptr, 0, nullptr, nullptr );
                if ( len <= 0 )
                    continue;

                out.resize( static_cast<size_t>( len ) );
                WideCharToMultiByte(
                    CP_UTF8, 0, buffer.data( ), length, out.data( ), len, nullptr, nullptr );
                return true;
            }

            return false;
        }

        bool read_dotnet_string( uint64_t field_address, std::string& out ) const {
            out.clear( );
            if ( !field_address )
                return false;

            const uint64_t string_obj = m_is_32bit
                ? read<uint32_t>( field_address )
                : read<uint64_t>( field_address );

            if ( string_obj && read_dotnet_string_object( string_obj, out ) )
                return true;

            return read_dotnet_string_object( field_address, out );
        }

        static int32_t find_pid_by_name( const wchar_t* name ) {
            HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
            if ( snap == INVALID_HANDLE_VALUE )
                return 0;

            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof( entry );

            int32_t pid = 0;
            if ( Process32FirstW( snap, &entry ) ) {
                do {
                    if ( _wcsicmp( entry.szExeFile, name ) == 0 ) {
                        pid = static_cast<int32_t>( entry.th32ProcessID );
                        break;
                    }
                } while ( Process32NextW( snap, &entry ) );
            }

            CloseHandle( snap );
            return pid;
        }

    private:
        const c_syscall* m_syscall = nullptr;
        HANDLE m_handle = nullptr;
        int32_t m_pid = 0;
        bool m_is_32bit = false;
        bool m_use_fallback = false;
    };

}