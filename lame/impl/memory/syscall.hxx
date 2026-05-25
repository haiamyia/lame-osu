#pragma once

#include <impl/memory/ntdll.hxx>
#include <Windows.h>
#include <cstdint>
#include <cstring>

#ifndef NTSTATUS
using NTSTATUS = LONG;
#endif

#ifndef STATUS_NOT_IMPLEMENTED
#define STATUS_NOT_IMPLEMENTED ((NTSTATUS)0xC0000002L)
#endif

typedef struct _CLIENT_ID {
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} CLIENT_ID, *PCLIENT_ID;

typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWSTR Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _OBJECT_ATTRIBUTES {
    ULONG Length;
    HANDLE RootDirectory;
    PUNICODE_STRING ObjectName;
    ULONG Attributes;
    PVOID SecurityDescriptor;
    PVOID SecurityQualityOfService;
} OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

#ifndef OBJ_CASE_INSENSITIVE
#define OBJ_CASE_INSENSITIVE 0x00000040L
#endif

inline void InitializeObjectAttributes(
    POBJECT_ATTRIBUTES initialized_attributes,
    PUNICODE_STRING object_name,
    ULONG attributes,
    HANDLE root_directory,
    PVOID security_descriptor ) {
    initialized_attributes->Length = sizeof( OBJECT_ATTRIBUTES );
    initialized_attributes->RootDirectory = root_directory;
    initialized_attributes->Attributes = attributes;
    initialized_attributes->ObjectName = object_name;
    initialized_attributes->SecurityDescriptor = security_descriptor;
    initialized_attributes->SecurityQualityOfService = nullptr;
}

#pragma comment( lib, "ntdll.lib" )

namespace memory {

    class c_syscall {
    public:
        c_syscall( ) = default;

        ~c_syscall( ) {
            if ( m_stub ) {
                VirtualFree( m_stub, 0, MEM_RELEASE );
                m_stub = nullptr;
            }
        }

        c_syscall( const c_syscall& ) = delete;
        c_syscall& operator=( const c_syscall& ) = delete;

        bool setup( c_clean_ntdll& ntdll ) {
            m_nt_read = ntdll.syscall_index( "NtReadVirtualMemory" );
            m_nt_write = ntdll.syscall_index( "NtWriteVirtualMemory" );
            m_nt_open = ntdll.syscall_index( "NtOpenProcess" );
            m_nt_query = ntdll.syscall_index( "NtQueryInformationProcess" );

            if ( !m_nt_read || !m_nt_open ) {
                return false;
            }

            m_stub = VirtualAlloc( nullptr, 32, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE );
            if ( !m_stub ) {
                return false;
            }

            static const uint8_t k_template[] = {
                0x4C, 0x8B, 0xD1,
                0xB8, 0x00, 0x00, 0x00, 0x00,
                0x0F, 0x05,
                0xC3
            };
            std::memcpy( m_stub, k_template, sizeof( k_template ) );

            return true;
        }

        [[nodiscard]] bool ready( ) const { return m_stub != nullptr && m_nt_read && m_nt_open; }

        template<typename... args_t>
        NTSTATUS invoke( uint32_t index, args_t... args ) const {
            if ( !m_stub )
                return STATUS_NOT_IMPLEMENTED;

            *reinterpret_cast<uint32_t*>( static_cast<uint8_t*>( m_stub ) + 4 ) = index;

            using fn_t = NTSTATUS( NTAPI* )( args_t... );
            return reinterpret_cast<fn_t>( m_stub )( args... );
        }

        NTSTATUS nt_read_virtual_memory(
            HANDLE process, void* base, void* buffer, SIZE_T size, SIZE_T* bytes_read ) const {
            return invoke( m_nt_read, process, base, buffer, size, bytes_read );
        }

        NTSTATUS nt_open_process(
            PHANDLE process_handle, ACCESS_MASK access, POBJECT_ATTRIBUTES obj_attr, PCLIENT_ID client_id ) const {
            return invoke( m_nt_open, process_handle, access, obj_attr, client_id );
        }

        NTSTATUS nt_query_information_process(
            HANDLE process, int info_class, PVOID info, ULONG len, PULONG ret_len ) const {
            if ( !m_nt_query )
                return STATUS_NOT_IMPLEMENTED;
            return invoke( m_nt_query, process, info_class, info, len, ret_len );
        }

    private:
        void* m_stub = nullptr;
        uint32_t m_nt_read = 0;
        uint32_t m_nt_write = 0;
        uint32_t m_nt_open = 0;
        uint32_t m_nt_query = 0;
    };

}