#include <impl/includes.hxx>
#include <impl/ui/overlay.hxx>
#include <core/threads/cache.hxx>
#include <mmsystem.h>

#pragma comment( lib, "winmm.lib" )

int WINAPI wWinMain( HINSTANCE instance, HINSTANCE, PWSTR, int ) {
    ::timeBeginPeriod( 1 );

    threads::c_cache cache;
    if ( !cache.init( ) ) {
        MessageBoxW( nullptr, L"init failed", L"lame", MB_ICONERROR | MB_OK );
        return 1;
    }

    ui::c_overlay overlay;
    if ( !overlay.create( instance ) ) {
        MessageBoxW( nullptr, L"create failed", L"lame", MB_ICONERROR | MB_OK );
        return 1;
    }

    overlay.set_cache( &cache );
    overlay.set_snapshot_source( [ &cache ]() {
        return cache.get_snapshot( );
    } );
    cache.set_module_tick( [ &overlay ]( const osu::full_snapshot_t& snap ) {
        overlay.tick_modules( snap );
    } );

    cache.start( );

    while ( overlay.pump( ) ) {
        Sleep( 1 );
    }

    cache.stop( );
    overlay.destroy( );

    ::timeEndPeriod( 1 );
    return 0;
}