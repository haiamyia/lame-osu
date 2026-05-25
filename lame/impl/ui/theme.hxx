#pragma once

#include <imgui.h>
#include <cmath>

namespace ui::theme {

    inline ImVec4 accent() { return ImVec4( 0.55f, 0.35f, 0.95f, 1.f ); }
    inline ImVec4 accent_dim() { return ImVec4( 0.35f, 0.22f, 0.62f, 1.f ); }
    inline ImVec4 panel_bg() { return ImVec4( 0.08f, 0.08f, 0.10f, 0.94f ); }

    inline float ease_out_cubic( float t ) {
        t = std::clamp( t, 0.f, 1.f );
        const float f = t - 1.f;
        return f * f * f + 1.f;
    }

    inline void apply_style( ) {
        ImGuiStyle& style = ImGui::GetStyle( );
        style.WindowRounding = 10.f;
        style.FrameRounding = 6.f;
        style.GrabRounding = 6.f;
        style.WindowBorderSize = 0.f;
        style.FramePadding = ImVec2( 10, 6 );
        style.ItemSpacing = ImVec2( 10, 8 );

        ImVec4* colors = style.Colors;
        colors[ ImGuiCol_WindowBg ] = panel_bg( );
        colors[ ImGuiCol_FrameBg ] = ImVec4( 0.14f, 0.14f, 0.18f, 1.f );
        colors[ ImGuiCol_FrameBgHovered ] = ImVec4( 0.20f, 0.18f, 0.28f, 1.f );
        colors[ ImGuiCol_FrameBgActive ] = accent_dim( );
        colors[ ImGuiCol_CheckMark ] = accent( );
        colors[ ImGuiCol_SliderGrab ] = accent( );
        colors[ ImGuiCol_SliderGrabActive ] = accent( );
        colors[ ImGuiCol_Button ] = ImVec4( 0.18f, 0.16f, 0.26f, 1.f );
        colors[ ImGuiCol_ButtonHovered ] = accent_dim( );
        colors[ ImGuiCol_ButtonActive ] = accent( );
        colors[ ImGuiCol_Header ] = accent_dim( );
        colors[ ImGuiCol_HeaderHovered ] = accent( );
        colors[ ImGuiCol_HeaderActive ] = accent( );
        colors[ ImGuiCol_Tab ] = ImVec4( 0.12f, 0.11f, 0.16f, 1.f );
        colors[ ImGuiCol_TabHovered ] = accent_dim( );
        colors[ ImGuiCol_TabActive ] = accent( );
        colors[ ImGuiCol_Text ] = ImVec4( 0.92f, 0.92f, 0.96f, 1.f );
    }

}