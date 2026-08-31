#pragma once

#include <mutex>
#include <array>

#include "..\..\game\datatypes\game_data.hpp"
#include "..\..\game\datatypes\matrix.hpp"
#include "..\..\game\datatypes\vector3.hpp"
#include "..\..\game\datatypes\vector2.hpp"
#include "..\..\game\offsets.hpp"
#include "..\..\game\sdk.hpp"
#include "..\..\utils\render\render.hpp"
#include "..\..\features\misc\misc.hpp"

namespace esp
{
    inline void draw_crosshair( ) {
        static float centerX = sdk::screen_width / 2.0f;
        static float centerY = sdk::screen_height / 2.0f;

        ImU32 color = IM_COL32( 0, 255, 0, 255 );

        int size = 10;
        int gap = 1;
        float thickness = 1.0f;

        g_render->line( centerX - size, centerY, centerX - gap, centerY, color, thickness );
        g_render->line( centerX + gap, centerY, centerX + size, centerY, color, thickness );
        g_render->line( centerX, centerY - size, centerX, centerY - gap, color, thickness );
        g_render->line( centerX, centerY + gap, centerX, centerY + size, color, thickness );
    }

    // 从共享数据中拷贝一份用于渲染（线程安全）
    inline auto GetRenderData( ) -> SGameData
    {
        std::lock_guard<std::mutex> lock( misc::g_gameMutex );
        return misc::g_gameData;
    }

    // 渲染主函数 — 每帧读取最新视角矩阵，对预计算的世界顶点做 world_to_screen
    inline auto run( ) -> void
    {
        draw_crosshair( );

        // 从共享数据中拷贝一份
        const SGameData renderData = GetRenderData( );
        if ( !renderData.bIsValid )
            return;

        // 每帧读取最新的视角矩阵（单次 DMA 读取，开销极小）
        const auto camera_matrix = sdk::cGame->camera->getCameraMatrix( );

        // 飞机模式：绘制炸弹落点
        if ( renderData.bLocalIsPlane && renderData.bHasBombImpact )
        {
            vec2_t screen_position;
            if ( g_render->world_to_screen( renderData.bombImpactPoint, screen_position, camera_matrix ) )
                g_render->circle( screen_position.x, screen_position.y, 6.0f, IM_COL32( 255, 0, 200, 255 ), 16 );
        }

        // 遍历预计算的 unit 数据进行渲染
        for ( const SImGuiUnit& unit : renderData.units )
        {
            if ( !unit.bValidEnemy )
                continue;

            if ( unit.distance >= 1250.0f )
                continue;

            // 对预计算的世界顶点做 world_to_screen（使用最新视角矩阵）
            std::array<vec2_t, 8> screen_corners;
            bool corners_visible[8] = {};
            int visible_count = 0;

            for ( size_t i = 0; i < unit.worldCorners.size( ); ++i )
            {
                if ( g_render->world_to_screen( unit.worldCorners[i], screen_corners[i], camera_matrix ) )
                {
                    corners_visible[i] = true;
                    ++visible_count;
                }
            }

            // 只要至少 1 个顶点可见就画框
            if ( visible_count > 0 )
            {
                // 计算屏幕边界
                float box_bottom_y = 0.0f;
                float box_top_y = 0.0f;
                float box_right_x = 0.0f;
                bool first = true;

                for ( size_t i = 0; i < screen_corners.size( ); ++i )
                {
                    if ( !corners_visible[i] )
                        continue;

                    if ( first )
                    {
                        box_bottom_y = screen_corners[i].y;
                        box_top_y    = screen_corners[i].y;
                        box_right_x  = screen_corners[i].x;
                        first = false;
                    }
                    else
                    {
                        box_bottom_y = max( box_bottom_y, screen_corners[i].y );
                        box_top_y    = min( box_top_y,    screen_corners[i].y );
                        box_right_x  = max( box_right_x,  screen_corners[i].x );
                    }
                }

                // 画可见顶点之间的线框边
                ImU32 box_color = IM_COL32(255, 0, 0, 255);
                // 底面四条边
                if ( corners_visible[0] && corners_visible[1] ) g_render->line( screen_corners[0].x, screen_corners[0].y, screen_corners[1].x, screen_corners[1].y, box_color, 1.0f );
                if ( corners_visible[1] && corners_visible[3] ) g_render->line( screen_corners[1].x, screen_corners[1].y, screen_corners[3].x, screen_corners[3].y, box_color, 1.0f );
                if ( corners_visible[3] && corners_visible[2] ) g_render->line( screen_corners[3].x, screen_corners[3].y, screen_corners[2].x, screen_corners[2].y, box_color, 1.0f );
                if ( corners_visible[2] && corners_visible[0] ) g_render->line( screen_corners[2].x, screen_corners[2].y, screen_corners[0].x, screen_corners[0].y, box_color, 1.0f );
                // 顶面四条边
                if ( corners_visible[4] && corners_visible[5] ) g_render->line( screen_corners[4].x, screen_corners[4].y, screen_corners[5].x, screen_corners[5].y, box_color, 1.0f );
                if ( corners_visible[5] && corners_visible[7] ) g_render->line( screen_corners[5].x, screen_corners[5].y, screen_corners[7].x, screen_corners[7].y, box_color, 1.0f );
                if ( corners_visible[7] && corners_visible[6] ) g_render->line( screen_corners[7].x, screen_corners[7].y, screen_corners[6].x, screen_corners[6].y, box_color, 1.0f );
                if ( corners_visible[6] && corners_visible[4] ) g_render->line( screen_corners[6].x, screen_corners[6].y, screen_corners[4].x, screen_corners[4].y, box_color, 1.0f );
                // 四条竖边
                if ( corners_visible[0] && corners_visible[4] ) g_render->line( screen_corners[0].x, screen_corners[0].y, screen_corners[4].x, screen_corners[4].y, box_color, 1.0f );
                if ( corners_visible[1] && corners_visible[5] ) g_render->line( screen_corners[1].x, screen_corners[1].y, screen_corners[5].x, screen_corners[5].y, box_color, 1.0f );
                if ( corners_visible[2] && corners_visible[6] ) g_render->line( screen_corners[2].x, screen_corners[2].y, screen_corners[6].x, screen_corners[6].y, box_color, 1.0f );
                if ( corners_visible[3] && corners_visible[7] ) g_render->line( screen_corners[3].x, screen_corners[3].y, screen_corners[7].x, screen_corners[7].y, box_color, 1.0f );

                // 距离显示（白色，米数格式）
                char distance_text[16];
                snprintf( distance_text, sizeof( distance_text ), "%dm", static_cast<int>(unit.distance) );

                // 计算单位中心点屏幕坐标用于文字定位
                vec2_t unit_screen;
                g_render->world_to_screen( unit.worldOrigin, unit_screen, camera_matrix );

                const vec2_t text_position = {
                    unit_screen.x,
                    box_bottom_y + 5.0f
                };

                g_render->text( text_position, IM_COL32( 255, 255, 255, 255 ), 0, distance_text, g_render->fonts( ).m_esp );

                // 装填时间显示（青色）
                char reload_text[16];
                constexpr float stat = ( 10.f / 16 );
                float progress = stat * unit.reloadTime;

                snprintf( reload_text, sizeof( reload_text ), "%.1fs", progress );

                const vec2_t reload_pos = {
                    unit_screen.x,
                    box_bottom_y + 20.0f
                };

                g_render->text( reload_pos, IM_COL32( 0, 200, 255, 255 ), 0, reload_text, g_render->fonts( ).m_esp );
            }

            // aimbot 预测绘制（使用预计算的瞄准点）
            if ( misc::bAimbotEnabled && unit.bHasAimPoint )
            {
                vec2_t aimScreen;
                if ( g_render->world_to_screen( unit.aimPoint, aimScreen, camera_matrix ) )
                {
                    // 黄色小方块标记预测命中点
                    g_render->rect( aimScreen.x - 4, aimScreen.y - 4, 8, 8, IM_COL32( 255, 255, 0, 200 ), 2.0f );

                    // 红色连线指向目标
                    vec2_t unitScreen;
                    if ( g_render->world_to_screen( unit.worldOrigin, unitScreen, camera_matrix ) )
                        g_render->line( unitScreen.x, unitScreen.y, aimScreen.x, aimScreen.y, IM_COL32( 255, 0, 0, 200 ), 2.0f );
                }
            }
        }
    }
}
