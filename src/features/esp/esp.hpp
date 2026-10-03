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
        int totalUnits   = static_cast<int>(renderData.units.size());
        int validEnemies = 0;
        int drawnBoxes   = 0;
        int drawnPred    = 0;
        for ( const SImGuiUnit& unit : renderData.units )
        {
            if ( !unit.bValidEnemy )
                continue;
            ++validEnemies;

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
                g_render->world_to_screen( unit.worldOrigin, unit_screen, camera_matrix );

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
			if ( misc::bBallisticPrediction && unit.bHasAimPoint )
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
