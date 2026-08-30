#pragma once

#include "..\..\game\datatypes\game_data.hpp"
#include "..\..\game\datatypes\matrix.hpp"
#include "..\..\game\datatypes\vector3.hpp"
#include "..\..\game\datatypes\vector2.hpp"
#include "..\..\utils\render\render.hpp"
#include "..\..\features\misc\misc.hpp"

namespace esp
{
    inline std::array< vec3_t, 8 > calculate_bbox_corners( const vec3_t& position, const vec3_t& bbmin, const vec3_t& bbmax, const matrix3x4_t& rot )
    {
        const auto r = rot.right;
        const auto f = rot.forward;
        const auto u = rot.up;

        const vec3_t rx0 = { r.x * bbmin.x, r.y * bbmin.x, r.z * bbmin.x };
        const vec3_t rx1 = { r.x * bbmax.x, r.y * bbmax.x, r.z * bbmax.x };
        const vec3_t fy0 = { f.x * bbmin.y, f.y * bbmin.y, f.z * bbmin.y };
        const vec3_t fy1 = { f.x * bbmax.y, f.y * bbmax.y, f.z * bbmax.y };
        const vec3_t uz0 = { u.x * bbmin.z, u.y * bbmin.z, u.z * bbmin.z };
        const vec3_t uz1 = { u.x * bbmax.z, u.y * bbmax.z, u.z * bbmax.z };

        const auto make = [ & ]( const vec3_t& rx, const vec3_t& fy, const vec3_t& uz ) -> vec3_t {
            return {
                position.x + rx.x + fy.x + uz.x,
                position.y + rx.y + fy.y + uz.y,
                position.z + rx.z + fy.z + uz.z
            };
        };

        return { {
            make( rx0, fy0, uz0 ),
            make( rx1, fy0, uz0 ),
            make( rx0, fy1, uz0 ),
            make( rx1, fy1, uz0 ),
            make( rx0, fy0, uz1 ),
            make( rx1, fy0, uz1 ),
            make( rx0, fy1, uz1 ),
            make( rx1, fy1, uz1 ),
        } };
    }

    inline void draw_wireframe_box( const std::array< vec2_t, 8 >& corners, ImU32 color, float thickness ) {

        g_render->line( corners[ 0 ].x, corners[ 0 ].y, corners[ 1 ].x, corners[ 1 ].y, color, thickness );
        g_render->line( corners[ 1 ].x, corners[ 1 ].y, corners[ 3 ].x, corners[ 3 ].y, color, thickness );
        g_render->line( corners[ 3 ].x, corners[ 3 ].y, corners[ 2 ].x, corners[ 2 ].y, color, thickness );
        g_render->line( corners[ 2 ].x, corners[ 2 ].y, corners[ 0 ].x, corners[ 0 ].y, color, thickness );

        g_render->line( corners[ 4 ].x, corners[ 4 ].y, corners[ 5 ].x, corners[ 5 ].y, color, thickness );
        g_render->line( corners[ 5 ].x, corners[ 5 ].y, corners[ 7 ].x, corners[ 7 ].y, color, thickness );
        g_render->line( corners[ 7 ].x, corners[ 7 ].y, corners[ 6 ].x, corners[ 6 ].y, color, thickness );
        g_render->line( corners[ 6 ].x, corners[ 6 ].y, corners[ 4 ].x, corners[ 4 ].y, color, thickness );

        g_render->line( corners[ 0 ].x, corners[ 0 ].y, corners[ 4 ].x, corners[ 4 ].y, color, thickness );
        g_render->line( corners[ 1 ].x, corners[ 1 ].y, corners[ 5 ].x, corners[ 5 ].y, color, thickness );
        g_render->line( corners[ 2 ].x, corners[ 2 ].y, corners[ 6 ].x, corners[ 6 ].y, color, thickness );
        g_render->line( corners[ 3 ].x, corners[ 3 ].y, corners[ 7 ].x, corners[ 7 ].y, color, thickness );
    }

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

    // 渲染主函数 — 纯绘制，无内存读取
    inline auto run( ) -> void
    {
        draw_crosshair( );

        // 从共享数据中拷贝一份
        const SGameData renderData = GetRenderData( );
        if ( !renderData.bIsValid )
            return;

        const matrix4x4_t& camera_matrix = renderData.gameCtx.viewMatrix;

        // 飞机模式：绘制炸弹落点
        if ( renderData.bLocalIsPlane && renderData.bHasBombImpact )
        {
            vec2_t screen_position;
            if ( g_render->world_to_screen( renderData.bombImpactPoint, screen_position, camera_matrix ) )
                g_render->circle( screen_position.x, screen_position.y, 6.0f, IM_COL32( 255, 0, 200, 255 ), 16.0f );
        }

        // 遍历预计算的 unit 数据进行渲染
        for ( const SImGuiUnit& unit : renderData.units )
        {
            if ( !unit.bValidEnemy )
                continue;

            if ( !unit.bOnScreen )
                continue;

            if ( unit.distance >= 1250.0f )
                continue;

            // 使用预计算的屏幕坐标绘制 3D 框
            // 将预计算的 SImGuiVert 数组转为 vec2_t 数组
            std::array< vec2_t, 8 > screen_corners;
            bool all_corners_visible = true;

            for ( size_t i = 0; i < unit.screenBoxVerts.size( ); ++i )
            {
                if ( !unit.screenBoxVerts[ i ].bOnScreen )
                {
                    all_corners_visible = false;
                    break;
                }
                screen_corners[ i ] = unit.screenBoxVerts[ i ].origin;
            }

            if ( all_corners_visible )
            {
                draw_wireframe_box( screen_corners, IM_COL32( 255, 0, 0, 255 ), 1.0f );
            }

            // aimbot 绘制（使用预计算的瞄准点）
            if ( misc::bAimbotEnabled && unit.bHasAimPoint )
            {
                vec2_t aimScreen;
                if ( g_render->world_to_screen( unit.aimPoint, aimScreen, camera_matrix ) )
                {
                    g_render->rect( aimScreen.x - 2, aimScreen.y - 2, 4, 4, IM_COL32( 255, 255, 0, 150 ), 4.0f );

                    // 从目标到瞄准点画线
                    g_render->line( unit.screenOrigin.x, unit.screenOrigin.y, aimScreen.x, aimScreen.y, IM_COL32( 255, 0, 0, 200 ), 2.0f );
                }
            }
        }
    }
}
