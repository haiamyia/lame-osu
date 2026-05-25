#pragma once

#include <core/beatmap/i_beatmap_provider.hxx>
#include <core/beatmap/stable_parser.hxx>
#include <core/beatmap/lazer_index.hxx>
#include <core/beatmap/lazer_ruleset_reader.hxx>
#include <impl/defs/offsets_lazer.hxx>
#include <filesystem>
#include <algorithm>
#include <cctype>

namespace beatmap {

    class c_lazer_memory : public i_beatmap_provider {
    public:
        void set_offsets( const offsets::lazer::table_t* offsets ) { m_offsets = offsets; }

        void set_data_dir( std::wstring path ) {
            if ( path != m_data_dir ) {
                m_data_dir = std::move( path );
                m_index.invalidate( );
            }
        }

        [[nodiscard]] const std::wstring& data_dir( ) const { return m_data_dir; }
        [[nodiscard]] const std::wstring& last_beatmap_path( ) const { return m_last_beatmap_path; }

        void warm_index( ) {
            if ( !m_data_dir.empty( ) )
                m_index.ensure_built( m_data_dir );
        }

        void invalidate_cache( ) {
            m_last_sig.clear( );
            m_cached = {};
            m_last_fail_sig.clear( );
        }

        void refresh_index( ) {
            m_index.invalidate( );
            if ( !m_data_dir.empty( ) )
                m_index.ensure_built( m_data_dir );
        }

        bool try_load( memory::c_process& process, const osu::game_snapshot_t& game, osu::beatmap_data_t& out ) override {
            out = {};
            out.map_id = game.map_id;
            out.set_id = game.set_id;

            const bool has_map =
                game.map_id > 0 || !game.beatmap_hash.empty( ) ||
                ( game.set_id > 0 && !game.beatmap_version.empty( ) );

            if ( !has_map )
                return false;

            const std::string sig =
                std::to_string( game.map_id ) + "|" + std::to_string( game.set_id ) + "|" +
                game.beatmap_hash + "|" + game.beatmap_version;

            const bool in_play = game.cur_state == osu::game_state_t::play;

            if ( !in_play && sig == m_last_sig && !m_cached.objects.empty( ) ) {
                out = m_cached;
                return true;
            }

            if ( m_offsets && m_offsets->has_hitobject_offsets( ) ) {
                c_lazer_ruleset_reader ruleset_reader;
                if ( ruleset_reader.try_load( process, game, *m_offsets, out ) ) {
                    m_last_sig = sig;
                    m_cached = out;
                    m_last_fail_sig.clear( );
                    m_last_beatmap_path.clear( );
                    return true;
                }
            }

            if ( m_data_dir.empty( ) )
                return false;

            m_index.ensure_built( m_data_dir );

            std::wstring path;

            if ( !game.beatmap_hash.empty( ) ) {
                path = find_hash_path( m_data_dir, game.beatmap_hash );
                if ( path.empty( ) )
                    path = m_index.find_by_hash( game.beatmap_hash );
            }

            if ( path.empty( ) && game.map_id > 0 )
                path = m_index.find_by_map_id( game.map_id );

            if ( path.empty( ) && game.set_id > 0 && !game.beatmap_version.empty( ) )
                path = m_index.find_by_set_and_version( game.set_id, game.beatmap_version );

            if ( path.empty( ) ) {
                out.error = "lazer beatmap file not found";
                if ( sig != m_last_fail_sig ) {
                    m_last_fail_sig = sig;
                    refresh_index( );
                }
                return false;
            }

            m_last_fail_sig.clear( );

            if ( !m_parser.load_from_path( path, game, out ) ) {
                out.error = "failed to parse lazer beatmap";
                return false;
            }

            m_last_beatmap_path = path;
            m_last_sig = sig;
            m_cached = out;

            return true;
        }

    private:
        const offsets::lazer::table_t* m_offsets = nullptr;
        std::wstring m_data_dir;
        std::wstring m_last_beatmap_path;
        std::string m_last_sig;
        c_stable_parser m_parser;
        c_lazer_index m_index;
        osu::beatmap_data_t m_cached;
        std::string m_last_fail_sig;

        static std::wstring find_hash_path( const std::wstring& data_dir, const std::string& hash_in ) {
            if ( data_dir.empty( ) || hash_in.empty( ) )
                return L"";

            std::string hash = hash_in;
            std::transform( hash.begin( ), hash.end( ), hash.begin( ),
                []( unsigned char c ) { return static_cast<char>( std::tolower( c ) ); } );

            const auto hash_wide = std::wstring( hash.begin( ), hash.end( ) );
            const auto files_root = std::filesystem::path( data_dir ) / L"files";

            if ( hash.size( ) >= 1 ) {
                auto candidate = files_root / std::wstring( 1, hash[ 0 ] ) / hash_wide;
                if ( std::filesystem::is_regular_file( candidate ) )
                    return candidate.wstring( );
            }

            if ( hash.size( ) >= 2 ) {
                auto candidate = files_root / std::wstring( hash.begin( ), hash.begin( ) + 2 ) / hash_wide;
                if ( std::filesystem::is_regular_file( candidate ) )
                    return candidate.wstring( );
            }

            return L"";
        }
    };

}