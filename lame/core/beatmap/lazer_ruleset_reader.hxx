#pragma once

#include <impl/defs/offsets_lazer.hxx>
#include <impl/memory/process.hxx>
#include <impl/util/playfield.hxx>
#include <impl/memory/input.hxx>
#include <algorithm>
#include <cmath>
#include <vector>

namespace beatmap {

    class c_lazer_ruleset_reader {
    public:
        bool try_load(
            memory::c_process& process,
            const osu::game_snapshot_t& game,
            const offsets::lazer::table_t& off,
            osu::beatmap_data_t& out ) {

            if ( !off.has_hitobject_offsets( ) )
                return false;

            if ( game.drawable_ruleset == 0 || game.cur_state != osu::game_state_t::play )
                return false;

            const auto beatmap =
                process.read<uint64_t>( game.drawable_ruleset + off.drawable_osu_beatmap );
            if ( !beatmap )
                return false;

            const auto hit_objects_list =
                process.read<uint64_t>( beatmap + off.beatmap_hit_objects );
            if ( !hit_objects_list )
                return false;

            const auto items =
                process.read<uint64_t>( hit_objects_list + off.list_items );
            const auto count = process.read<int32_t>( hit_objects_list + off.list_size );
            if ( !items || count <= 0 || count > 100000 )
                return false;

            out.screen_width = 1920;
            out.screen_height = 1080;
            playfield::get_window_size( input::target_window( ), out.screen_width, out.screen_height );

            std::vector<osu::hit_object_t> objects;
            objects.reserve( static_cast<size_t>( count ) );

            for ( int32_t i = 0; i < count; ++i ) {
                const auto obj_ptr =
                    process.read<uint64_t>( items + off.array_first_element + static_cast<uint64_t>( i ) * 8 );
                if ( !obj_ptr )
                    continue;

                const auto start_bindable = process.read<uint64_t>(
                    obj_ptr + off.hit_object_start_time_bindable );
                if ( !start_bindable )
                    continue;

                const auto start_time_raw =
                    process.read<double>( start_bindable + off.bindable_value );
                if ( !std::isfinite( start_time_raw ) )
                    continue;

                float x = 0.f;
                float y = 0.f;
                if ( off.osu_hit_object_position != 0 ) {
                    const auto position_field = process.read<uint64_t>(
                        obj_ptr + off.osu_hit_object_position );
                    if ( position_field ) {
                        const auto position_bindable = process.read<uint64_t>(
                            position_field + off.hit_object_property_bindable );
                        if ( position_bindable ) {
                            x = process.read<float>( position_bindable + off.bindable_value );
                            y = process.read<float>( position_bindable + off.bindable_value + 4 );
                        }
                    }
                }

                osu::hit_object_t obj{};
                obj.start_time = static_cast<int32_t>( start_time_raw );
                obj.end_time = obj.start_time + 100;
                obj.x = x;
                obj.y = y;
                obj.type = static_cast<uint8_t>( osu::hit_object_type_t::circle );

                if ( off.slider_end_time != 0 ) {
                    const auto end_time_raw =
                        process.read<double>( obj_ptr + off.slider_end_time );
                    if ( std::isfinite( end_time_raw ) && end_time_raw > start_time_raw )
                        obj.end_time = static_cast<int32_t>( end_time_raw );
                }

                if ( obj.end_time > obj.start_time + 150 )
                    obj.type |= static_cast<uint8_t>( osu::hit_object_type_t::slider );

                project_to_screen( obj, out.screen_width, out.screen_height );
                objects.push_back( obj );
            }

            if ( objects.empty( ) )
                return false;

            std::sort( objects.begin( ), objects.end( ),
                []( const osu::hit_object_t& a, const osu::hit_object_t& b ) {
                    return a.start_time < b.start_time;
                } );

            out.objects = std::move( objects );
            out.loaded = true;
            out.beatmap_path = "(memory:DrawableRuleset.Beatmap.HitObjects)";

            return true;
        }

    private:
        static void project_to_screen( osu::hit_object_t& obj, int32_t sw, int32_t sh ) {
            const float playfield_height = sh * 0.8f;
            const float playfield_width = playfield_height * ( 4.f / 3.f );
            const float scale = playfield_width / 512.f;
            const float offset_x = ( sw - playfield_width ) * 0.5f;
            const float offset_y = ( sh - playfield_height ) * 0.5f;

            const float stack_offset = -static_cast<float>( obj.stack_index ) * 6.f * scale;
            obj.screen_x = offset_x + ( obj.x * scale ) + stack_offset;
            obj.screen_y = offset_y + ( obj.y * scale ) + stack_offset + 17.f;
        }
    };

}