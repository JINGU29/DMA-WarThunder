#pragma once

#include <mutex>
#include <array>
#include <cmath>

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

    // ── DamageModel 乘员/弹药/油箱/炮闩部件标记（Style 1：真实盒线框 + 投影最长边<9px 时兜底 16px 方框）──
    // 数据源：misc 数据线程从网格束链解码并预变换为世界角点（unit.partBoxes），此处仅投影绘制。
    // 开镜缩放自动保证：盒为世界空间（随投影放大）；兜底阈值为屏幕像素判定（放大后超阈即回真实盒形）。
    // 分级 LOD 防远距糊团（单车 ammo 部件多达 40~86 个，兜底框叠加糊成一团），按敌人 8 角大盒投影最长边定级：
    //   ≥kLodFullPx 全细节逐盒；kLodOffPx~kLodFullPx 弹药合并成单个红色包围框、其余照常；<kLodOffPx 全隐藏。
    inline auto draw_part_markers( const SImGuiUnit& unit, const matrix4x4_t& matrix, const vec3_t& exDelta ) -> void
    {
        if ( !misc::bPartMarkersCrew && !misc::bPartMarkersAmmo && !misc::bPartMarkersFuel && !misc::bPartMarkersBreech )
            return;

        // LOD 定级：敌人 8 角大盒（worldCorners+外推位移）屏幕投影范围
        constexpr float kLodFullPx = 150.0f;   // ≥此值 → 全细节（阈值按实测观感可调）
        constexpr float kLodOffPx  = 55.0f;    // <此值 → 部件标记全隐藏（只剩敌人8角大盒）
        std::array<vec2_t, 8> bs;
        for ( int i = 0; i < 8; ++i )
            if ( !g_render->world_to_screen( unit.worldCorners[i] + exDelta, bs[i], matrix ) )
                return;   // 大盒任一角在视点后 → 整个单位跳过
        float bMinX = bs[0].x, bMaxX = bs[0].x, bMinY = bs[0].y, bMaxY = bs[0].y;
        for ( int i = 1; i < 8; ++i )
        {
            bMinX = ( std::min )( bMinX, bs[i].x ); bMaxX = ( std::max )( bMaxX, bs[i].x );
            bMinY = ( std::min )( bMinY, bs[i].y ); bMaxY = ( std::max )( bMaxY, bs[i].y );
        }
        const float lodSide = ( std::max )( bMaxX - bMinX, bMaxY - bMinY );
        if ( lodSide < kLodOffPx )
            return;
        const bool fullLod = ( lodSide >= kLodFullPx );

        const ImU32 colCrew = IM_COL32( 255, 140, 26, 235 );    // 乘员=橙
        const ImU32 colAmmo = IM_COL32( 255, 45, 45, 235 );     // 弹药=红
        const ImU32 colFuel = IM_COL32( 80, 220, 90, 235 );     // 油箱=绿
        const ImU32 colBreech = IM_COL32( 255, 220, 60, 235 );  // 炮闩=黄
        constexpr float minPx = 9.0f;          // 投影最长边低于此值走兜底
        constexpr float fallbackHalf = 8.0f;   // 兜底框半边长（16px）

        // 中距弹药合并：累加所有 ammo 盒的屏幕包围范围，循环结束后画一个合并框
        float aMinX = 0.0f, aMaxX = 0.0f, aMinY = 0.0f, aMaxY = 0.0f;
        bool anyAmmo = false;

        for ( const auto& pb : unit.partBoxes )
        {
            // 按类别查开关（四独立开关：乘员/弹药/油箱/炮闩）
            const ImU32 col = ( pb.cls == EPartClass::Crew ) ? colCrew :
                              ( pb.cls == EPartClass::Ammo ) ? colAmmo :
                              ( pb.cls == EPartClass::Fuel ) ? colFuel : colBreech;
            const bool enabled = ( pb.cls == EPartClass::Crew ) ? misc::bPartMarkersCrew :
                                 ( pb.cls == EPartClass::Ammo ) ? misc::bPartMarkersAmmo :
                                 ( pb.cls == EPartClass::Fuel ) ? misc::bPartMarkersFuel :
                                                                  misc::bPartMarkersBreech;
            if ( !enabled )
                continue;

            std::array<vec2_t, 8> s;
            bool ok = true;
            for ( int i = 0; i < 8; ++i )
            {
                if ( !g_render->world_to_screen( pb.corners[i] + exDelta, s[i], matrix ) )
                {
                    ok = false;
                    break;
                }
            }
            if ( !ok )
                continue;

            // 中距档弹药：不逐盒画，并入屏幕包围范围（防 40~86 个兜底框糊团）
            if ( pb.cls == EPartClass::Ammo && !fullLod )
            {
                if ( !anyAmmo )
                {
                    aMinX = aMaxX = s[0].x; aMinY = aMaxY = s[0].y;
                    anyAmmo = true;
                }
                for ( int i = 0; i < 8; ++i )
                {
                    aMinX = ( std::min )( aMinX, s[i].x ); aMaxX = ( std::max )( aMaxX, s[i].x );
                    aMinY = ( std::min )( aMinY, s[i].y ); aMaxY = ( std::max )( aMaxY, s[i].y );
                }
                continue;
            }

            float minX = s[0].x, maxX = s[0].x, minY = s[0].y, maxY = s[0].y;
            for ( int i = 1; i < 8; ++i )
            {
                minX = ( std::min )( minX, s[i].x ); maxX = ( std::max )( maxX, s[i].x );
                minY = ( std::min )( minY, s[i].y ); maxY = ( std::max )( maxY, s[i].y );
            }

            const float maxSide = ( std::max )( maxX - minX, maxY - minY );
            if ( maxSide < minPx )
            {
                // 远距退化：投影质心 16px 兜底框（保住“分布”可读性）
                float cx = 0.0f, cy = 0.0f;
                for ( int i = 0; i < 8; ++i ) { cx += s[i].x; cy += s[i].y; }
                cx /= 8.0f; cy /= 8.0f;
                g_render->rect( cx - fallbackHalf, cy - fallbackHalf, fallbackHalf * 2.0f, fallbackHalf * 2.0f, col );
            }
            else
            {
                // 真实盒线框（12 边；角点位序 bit0=x bit1=y bit2=z，与敌人大盒一致）
                static const int edges[12][2] = { {0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7} };
                for ( const auto& e : edges )
                    g_render->line( s[e[0]].x, s[e[0]].y, s[e[1]].x, s[e[1]].y, col, 1.5f );
            }
        }

        // 中距弹药合并框（整个弹药舱群画一个红色包围框）
        if ( anyAmmo )
            g_render->rect( aMinX, aMinY, aMaxX - aMinX, aMaxY - aMinY, colAmmo );
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

        // 飞机模式：导弹 CCIP 落点（ballistics 容器 +0x1C9C 候选）
        if ( renderData.bLocalIsPlane && renderData.bHasRocketImpact )
        {
            vec2_t pip;
            if ( g_render->world_to_screen( renderData.rocketImpactPoint, pip, camera_matrix ) )
            {
                g_render->circle( pip.x, pip.y, 8.0f, IM_COL32( 255, 60, 255, 255 ), 24 );
                g_render->line( pip.x - 12, pip.y, pip.x + 12, pip.y, IM_COL32( 255, 60, 255, 220 ), 1.5f );
                g_render->line( pip.x, pip.y - 12, pip.x, pip.y + 12, IM_COL32( 255, 60, 255, 220 ), 1.5f );
            }
        }

        // ── 导弹/炸弹追踪：红=来袭(敌) 绿=己方，名字 + 距离 + 1.5s 弹道线 + 锁定目标连线 ──
        if ( misc::bMissileWarn && !renderData.missiles.empty( ) )
        {
            int incomingLocked = 0;
            for ( const auto& m : renderData.missiles )
            {
                    // 渲染线程外推：renderPos = samplePos + vel * (renderNow - sampleTime)
                    const auto renderNow = std::chrono::steady_clock::now( );
                    const float age = std::chrono::duration<float>( renderNow - m.sampleTime ).count( );
                    const vec3_t renderPos = m.position + m.velocity * age;
                vec2_t ms;
                if ( !g_render->world_to_screen( renderPos, ms, camera_matrix ) )
                    continue;

                const ImU32 col = m.own ? IM_COL32( 80, 255, 80, 255 ) : IM_COL32( 255, 60, 60, 255 );


                // 短弹道线：0.5s 速度投影（只标方向不画长线）
                const vec3_t ahead = renderPos + m.velocity * 0.5f;
                vec2_t as;
                if ( g_render->world_to_screen( ahead, as, camera_matrix ) )
                    g_render->line( ms.x, ms.y, as.x, as.y, col, 1.5f );

                // 菱形标记
                g_render->filled_rect( ms.x - 3, ms.y - 3, 6, 6, col, 0, 0 );
                g_render->rect( ms.x - 6, ms.y - 6, 12, 12, col, 1.5f );
                if ( !m.own && m.isLocked && m.targetIsLocal )
                    ++incomingLocked;

                // 名字 + 距离（名称解析失败时显示类型）
                char mlabel[ 96 ];
                const char* kind = m.isBomb ? "BOMB" : "MSL";
                if ( !m.name.empty( ) )
                    snprintf( mlabel, sizeof( mlabel ), "%s %.0fm", m.name.c_str( ), m.distance );
                else
                    snprintf( mlabel, sizeof( mlabel ), "%s%s %.0fm", m.own ? "[own] " : "", kind, m.distance );
                g_render->text( { ms.x + 10, ms.y - 6 },
                    m.own ? IM_COL32( 80, 255, 80, 255 ) : IM_COL32( 255, 90, 90, 255 ), 1, mlabel,
                    g_render->fonts( ).m_esp );

                // 锁定目标：弹→目标连线 + 目标名（本地目标红/他人橙）
                if ( m.hasTarget )
                {
                    vec2_t ts;
                    if ( g_render->world_to_screen( m.targetPos, ts, camera_matrix ) )
                        g_render->line( ms.x, ms.y, ts.x, ts.y,
                            m.targetIsLocal ? IM_COL32( 255, 40, 40, 220 ) : IM_COL32( 255, 150, 40, 180 ), 1.4f );
                }
            }

            // 来袭预警大字
            if ( incomingLocked > 0 )
            {
                const ImVec2 dsz = ImGui::GetIO( ).DisplaySize;
                char warn[ 64 ];
                snprintf( warn, sizeof( warn ), "!! MISSILE INCOMING x%d !!", incomingLocked );
                const ImVec2 tsz = ImGui::CalcTextSize( warn );
                g_render->text( { dsz.x * 0.5f - tsz.x * 0.5f, 36.0f },
                    IM_COL32( 255, 40, 40, 255 ), 2, warn, g_render->fonts( ).m_esp );
            }
        }


        // 遍历预计算的 unit 数据进行渲染
        int totalUnits   = static_cast<int>(renderData.units.size());
        int validEnemies = 0;
        int drawnBoxes   = 0;
        int drawnPred    = 0;
        for ( const SImGuiUnit& unit : renderData.units )
        {
            if ( !unit.bValidEnemy )
                continue;
            ++validEnemies;

            // ── 渲染端外推：box = 采样位置 + velocity × (now - sampleTime)，
            //    消除"数据线程采样 → 渲染帧"时间差导致的高速单位（飞机）跳动/滞后。
            //    ★age 上限必须 ≥ 数据线程轮次间隔（实测 ~1.1s）：上限小于间隔时，每个周期
            //    后半段外推被掐断、框掉回旧采样点，下一轮采样又弹回真实位置 → 前后跳动。
            //    速度合法性已在数据线程校验（有限且 <1500m/s），此处上限仅作最后保险。
            vec3_t exDelta{};
            {
                const float age = std::chrono::duration<float>(
                    std::chrono::steady_clock::now( ) - unit.sampleTime ).count( );
                const float spd = unit.velocity.length( );
                if ( spd > 1.0f && spd < 1500.0f && age > 0.0f && age < 2.0f )
                {
                    const vec3_t d = unit.velocity * age;
                    if ( d.length( ) < 2000.0f )   // 位移上限：垃圾速度漏网时保框不消失（丢框防护）
                        exDelta = d;
                }
            }
            const vec3_t renderOrigin = unit.worldOrigin + exDelta;

            // ── 飞机模式省开销：地面载具只显示名字+距离（不画 8 角框/部件标记/预测点，
            //    一帧省掉 30+ 单位 × (8 次投影+12 条线+部件盒) 的绘制调用）
            if ( renderData.bLocalIsPlane && unit.unitType != 0 )
            {
                vec2_t os;
                if ( g_render->world_to_screen( renderOrigin, os, camera_matrix ) )
                {
                    if ( !unit.vehicleName.empty( ) )
                        g_render->text( { os.x, os.y - 20.0f }, IM_COL32( 0, 255, 255, 255 ), 1,
                            unit.vehicleName.c_str( ), g_render->fonts( ).m_esp );
                    char distance_text[ 16 ];
                    snprintf( distance_text, sizeof( distance_text ), "%dm", static_cast<int>( unit.distance ) );
                    g_render->text( { os.x, os.y + 5.0f }, IM_COL32( 255, 255, 255, 255 ), 0,
                        distance_text, g_render->fonts( ).m_esp );
                }
                continue;
            }

            // 对预计算的世界顶点做 world_to_screen（使用最新视角矩阵；含外推平移）
            std::array<vec2_t, 8> screen_corners;
            bool corners_visible[8] = {};
            int visible_count = 0;

            for ( size_t i = 0; i < unit.worldCorners.size( ); ++i )
            {
                if ( g_render->world_to_screen( unit.worldCorners[i] + exDelta, screen_corners[i], camera_matrix ) )
                {
                    corners_visible[i] = true;
                    ++visible_count;
                }
            }

            // 只要至少 1 个顶点可见就画框
            if ( visible_count > 0 )
            {
                ++drawnBoxes;
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
				// 可见性颜色：可见=绿，被遮挡/不可见=红
				ImU32 box_color = unit.bVisible ? IM_COL32( 0, 255, 0, 255 ) : IM_COL32( 255, 0, 0, 255 );
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
                g_render->world_to_screen( renderOrigin, unit_screen, camera_matrix );

                // 载具名字显示（青色带描边，显示在框顶部上方）
                if ( !unit.vehicleName.empty( ) )
                {
                    const vec2_t name_pos = {
                        unit_screen.x,
                        box_top_y - 20.0f
                    };
                    g_render->text( name_pos, IM_COL32( 0, 255, 255, 255 ), outline, unit.vehicleName, g_render->fonts( ).m_esp );
                }

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

			// 弹道预测绘制：末端红色方框标预测命中点（提前量+下坠补偿后的位置）
			// 飞机模式下不画（飞机用炸弹落点紫圈/导弹 CCIP 洋红圈，不需要地面预测框）
			if ( misc::bBallisticPrediction && unit.bHasAimPoint && !renderData.bLocalIsPlane )
			{
				++drawnPred;
				vec2_t aimScreen;
				if ( g_render->world_to_screen( unit.aimPoint, aimScreen, camera_matrix ) )
				{
					// 红色方框标记预测命中点
					g_render->rect( aimScreen.x - 5, aimScreen.y - 5, 10, 10, IM_COL32( 255, 0, 0, 255 ), 2.0f );
					g_render->filled_rect( aimScreen.x - 1, aimScreen.y - 1, 2, 2, IM_COL32( 255, 0, 0, 220 ), 0, 0 );
				}
			}

			// ── DamageModel 部件标记：只在地面模式 + 地面载具(type=3)上画。
			//    飞机模式不画（部件无意义且省开销），地面模式下空中单位也不画（pilot_dm/_Clip_* 无用）──
			if ( !renderData.bLocalIsPlane && unit.unitType == 3 )
				draw_part_markers( unit, camera_matrix, exDelta );

			// 弹道落点部位：被选中的部件盒青色高亮（同样仅地面模式 + 地面载具）
			if ( !renderData.bLocalIsPlane && unit.unitType == 3
				&& misc::bBallisticPrediction && misc::ballisticAimPart > 0
				&& unit.bHasAimPoint && unit.aimPartIdx >= 0
				&& unit.aimPartIdx < static_cast< int >( unit.partBoxes.size( ) ) )
			{
				const auto& pb = unit.partBoxes[ unit.aimPartIdx ];
				std::array<vec2_t, 8> s;
				bool ok = true;
				for ( int c = 0; c < 8; ++c )
					if ( !g_render->world_to_screen( pb.corners[ c ] + exDelta, s[ c ], camera_matrix ) )
					{
						ok = false;
						break;
					}
				if ( ok )
				{
					// 角点位序 bit0=x bit1=y bit2=z（与部件标记一致），12 条棱
					static const int selEdges[ 12 ][ 2 ] = { {0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7} };
					for ( const auto& e : selEdges )
						g_render->line( s[ e[ 0 ] ].x, s[ e[ 0 ] ].y, s[ e[ 1 ] ].x, s[ e[ 1 ] ].y,
							IM_COL32( 60, 230, 255, 235 ), 2.2f );
				}
			}
		}

		// 节流日志（5 秒一次）：ESP 渲染计数器，用于诊断画面不显示的原因
		static auto s_lastEspLog = std::chrono::steady_clock::now();
		const auto s_now = std::chrono::steady_clock::now();
		if ( std::chrono::duration_cast<std::chrono::seconds>( s_now - s_lastEspLog ).count() >= 5 )
		{
			s_lastEspLog = s_now;
			const char* firstUnitName = "<none>";
			float firstUnitDist = 0.0f;
			float firstUnitVel = 0.0f;
			int firstUnitVis = 0;
			if ( validEnemies > 0 )
			{
				for ( const auto& u : renderData.units )
				{
					if ( !u.bValidEnemy ) continue;
					firstUnitName = u.vehicleName.empty() ? "<noname>" : u.vehicleName.c_str();
					firstUnitDist = u.distance;
					firstUnitVel = u.velocity.length();
					// 重新计算 visible_count 以便日志输出
					for ( size_t k = 0; k < u.worldCorners.size(); ++k )
					{
						vec2_t tmp;
						if ( g_render->world_to_screen( u.worldCorners[k], tmp, camera_matrix ) )
							++firstUnitVis;
					}
					break;
				}
			}
			const char* validity = renderData.bIsValid ? "VALID" : "INVALID";
			const uint8_t gs = sdk::cLocalPlayer->getGuiState();
			const char* gsStr = ( gs == GuiState::BATTLE ) ? "BATTLE" :
								( gs == GuiState::ALIVE ) ? "ALIVE" :
								( gs == GuiState::SPECTATE ) ? "SPEC" :
								( gs == GuiState::DEAD ) ? "DEAD" :
								( gs == GuiState::MENU ) ? "MENU" : "OTHER";
			const auto& cm = camera_matrix.m_matrix;
			char camBuf[160];
			snprintf( camBuf, sizeof( camBuf ), "cam[0][3]=%.2f [1][3]=%.2f [2][3]=%.2f [3][3]=%.2f [3][0..2]=(%.0f,%.0f,%.0f)",
				cm[0][3], cm[1][3], cm[2][3], cm[3][3], cm[3][0], cm[3][1], cm[3][2] );
			if ( validEnemies > 0 )
			{
				char unitCornersSample[160];
				const auto& wcu = renderData.units;
				for ( const auto& u : wcu )
				{
					if ( !u.bValidEnemy ) continue;
					const auto& w0 = u.worldCorners[0];
					const auto& w7 = u.worldCorners[7];
					snprintf( unitCornersSample, sizeof( unitCornersSample ),
						" unit0[0]=(%.0f,%.0f,%.0f) [7]=(%.0f,%.0f,%.0f)",
						w0.x, w0.y, w0.z, w7.x, w7.y, w7.z );
					break;
				}
				char* p = unitCornersSample + strlen( unitCornersSample );
				const auto& wcu2 = renderData.units;
				for ( const auto& u : wcu2 )
				{
					if ( !u.bValidEnemy ) continue;
				snprintf( p, sizeof( unitCornersSample ) - strlen( unitCornersSample ), " origin=(%.0f,%.0f,%.0f)",
					u.worldOrigin.x, u.worldOrigin.y, u.worldOrigin.z );
					break;
				}
				char unitCornersData[160] = "";
				char* p2 = unitCornersData;
				for ( const auto& u : renderData.units )
				{
					if ( !u.bValidEnemy ) continue;
					snprintf( p2, 160 - strlen( unitCornersData ), " origin=(%.0f,%.0f,%.0f)", u.worldOrigin.x, u.worldOrigin.y, u.worldOrigin.z );
					break;
				}
				const auto& firstUnitRef = [&]() -> const SImGuiUnit* {
					for ( const auto& u : renderData.units )
						if ( u.bValidEnemy ) return &u;
					return nullptr;
				}();
				char cornersFull[256] = "";
				if ( firstUnitRef )
				{
					const auto& u = *firstUnitRef;
					const auto& w0 = u.worldCorners[0];
					const auto& w7 = u.worldCorners[7];
					snprintf( cornersFull, sizeof( cornersFull ),
						" box[0]=(%.0f,%.0f,%.0f) box[7]=(%.0f,%.0f,%.0f) origin=(%.0f,%.0f,%.0f)",
						w0.x, w0.y, w0.z, w7.x, w7.y, w7.z,
						u.worldOrigin.x, u.worldOrigin.y, u.worldOrigin.z );
				}
				char firstUnitSample[320];
				snprintf( firstUnitSample, sizeof( firstUnitSample ),
					" first=%s %.0fm vel=%.1f visibleCorners=%d %s",
					firstUnitName, firstUnitDist, firstUnitVel, firstUnitVis, cornersFull );
				// 完整一行日志
				char fullLine[768];
				snprintf( fullLine, sizeof( fullLine ),
					"ESPRender: total=%d valid=%d boxes=%d pred=%d | gui=%s(%d) %s | localY=%.0f\n",
					totalUnits, validEnemies, drawnBoxes, drawnPred,
					gsStr, gs, firstUnitSample,
					renderData.localPosition.y );
				LOG( "%s", fullLine );
		}
		else
		{
			char line[256];
			snprintf( line, sizeof( line ),
				"ESPRender: total=%d valid=%d boxes=%d pred=%d | gui=%s(%d) NO_VALID_ENEMY | localPos=(%.0f,%.0f,%.0f)\n",
				totalUnits, validEnemies, drawnBoxes, drawnPred,
				gsStr, gs,
				renderData.localPosition.x, renderData.localPosition.y, renderData.localPosition.z );
			LOG( "%s", line );
			}
		}
    }
}
