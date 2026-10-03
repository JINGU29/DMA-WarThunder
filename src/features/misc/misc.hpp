#pragma once

#include <mutex>
#include <array>
#include <vector>
#include <cmath>
#include <chrono>

#include "..\..\game\datatypes\game_data.hpp"
#include "..\..\game\datatypes\matrix.hpp"
#include "..\..\game\datatypes\vector3.hpp"
#include "..\..\game\datatypes\vector2.hpp"
#include "..\..\game\offsets.hpp"
#include "..\..\game\classes\units.hpp"
#include "..\..\game\classes\entity.hpp"
#include "..\..\game\classes\game.hpp"
#include "..\..\game\classes\info.hpp"
#include "..\..\game\classes\movement.hpp"
#include "..\..\game\classes\ballistic.hpp"
#include "..\..\game\classes\camera.hpp"
#include "..\..\game\sdk.hpp"
#include "..\..\utils\render\render.hpp"

namespace misc
{
	// 共享游戏数据和互斥锁
	inline std::mutex g_gameMutex;
	inline SGameData g_gameData;

	// aimbot 开关
	inline bool bAimbotEnabled = false;

	// 弹道预测开关（提前量 + 下坠补偿，陆战坦克为主）
	inline bool bBallisticPrediction = true;

	// 判断是否为有效敌人
	inline auto is_valid_enemy( uint16_t unitState, uint8_t team ) -> bool
	{
		if ( unitState >= 2 )
			return false;

		if ( team == 0 )
			return false;

		const uint8_t local_team = sdk::cLocalPlayer->getLocalUnit( ).getTeam( );
		if ( team == local_team )
			return false;

		return true;
	}

	// 使用 scatter read 获取游戏上下文 (cGame, cCamera, viewMatrix)
	inline auto ScatterGame( SImGuiGame& outCtx ) -> bool
	{
		// 读取 cGame 指针
		uintptr_t cGame = TargetProcess->Read<uintptr_t>( baseAddr + offsets::globals::game_context );
		if ( !cGame )
			return false;

		// 读取 cCamera 指针
		uintptr_t cCamera = TargetProcess->Read<uintptr_t>( cGame + offsets::cgame_offsets::camera_offset );
		if ( !cCamera )
			return false;

		// 读取视角矩阵
		matrix4x4_t matrix;
		VMMDLL_SCATTER_HANDLE hScatter = TargetProcess->CreateScatterHandle( );
		if ( !hScatter )
			return false;

		if ( !TargetProcess->AddScatterReadRequest( hScatter, uint64_t( cCamera + offsets::cgame_offsets::camera_offsets::camera_matrix_offset ), &matrix, sizeof( matrix4x4_t ) )
			|| !TargetProcess->ExecuteReadScatter( hScatter, 0, true ) )
		{
			TargetProcess->CloseScatterHandle( hScatter );
			return false;
		}

		TargetProcess->CloseScatterHandle( hScatter );

		outCtx.cGame = cGame;
		outCtx.cCamera = cCamera;
		outCtx.viewMatrix = matrix;
		return true;
	}

	// GameUpdate：数据线程主函数 —— 批量 scatter read + 预计算
	inline auto GameUpdate( ) -> void
	{
		SGameData computedData;

		// --- 1. 检查 GUI 状态 ---
		const auto gui_state = sdk::cLocalPlayer->getGuiState( );

		// debug log（节流 5 秒）
		static auto last_debug_log = std::chrono::steady_clock::now( );
		auto now = std::chrono::steady_clock::now( );
		static uint8_t last_state = 0xFF;
		bool state_changed = ( gui_state != last_state );
		last_state = gui_state;

		if ( gui_state != GuiState::ALIVE && gui_state != GuiState::SPECTATE && gui_state != GuiState::SPEC && gui_state != GuiState::BATTLE )
		{
			if ( state_changed || std::chrono::duration_cast<std::chrono::seconds>( now - last_debug_log ).count( ) >= 5 )
			{
				const char* state_str = "UNKNOWN";
				switch ( gui_state )
				{
					case GuiState::NONE:        state_str = "NONE(lobby)"; break;
					case GuiState::MENU:        state_str = "MENU"; break;
					case GuiState::LOADING:     state_str = "LOADING"; break;
					case GuiState::SPAWN_MENU:  state_str = "SPAWN_MENU"; break;
					case GuiState::DEAD:        state_str = "DEAD"; break;
					case GuiState::SPECTATE:    state_str = "SPECTATE"; break;
					case GuiState::BATTLE:      state_str = "BATTLE"; break;
					default: break;
				}
				LOG( "Waiting for battle... state: %s (gui_state=%d)\n", state_str, gui_state );
				last_debug_log = now;
			}

			// 非战斗状态，重新初始化指针
			sdk::cLocalPlayer->init( );
			sdk::cGame->init( );

			std::lock_guard<std::mutex> lock( g_gameMutex );
			g_gameData.bIsValid = false;
			g_gameData.units.clear( );
			return;
		}

		// 进入战斗状态，记录日志
		if ( state_changed )
		{
			LOG( "Entered battle! (gui_state=%d)\n", gui_state );
			last_debug_log = now;
		}

		// --- 2. 获取游戏上下文 (cGame, cCamera, viewMatrix) ---
		SImGuiGame gameCtx;
		if ( !ScatterGame( gameCtx ) )
			return;

		computedData.gameCtx = gameCtx;

		// --- 3. 读取 unit 列表信息 ---
		const int unit_count = sdk::cGame->getUnitCount( );
		if ( !unit_count || unit_count > 10000 )
		{
			std::lock_guard<std::mutex> lock( g_gameMutex );
			g_gameData = computedData;
			g_gameData.bIsValid = true;
			return;
		}

		const uintptr_t unit_list_base = sdk::cGame->getUnitList( );
		if ( !unit_list_base )
			return;

		// --- 4. 全量 scatter read ---

		// 第一阶段：scatter read 所有 unit 指针
		VMMDLL_SCATTER_HANDLE hScatter = TargetProcess->CreateScatterHandle( );
		if ( !hScatter )
			return;

		std::vector<uintptr_t> unitPtrs( unit_count );
		for ( int i = 0; i < unit_count; i++ )
		{
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( unit_list_base + 0x8 * i ), &unitPtrs[i], sizeof( uintptr_t ) );
		}

		if ( !TargetProcess->ExecuteReadScatter( hScatter, 0, true ) )
		{
			TargetProcess->CloseScatterHandle( hScatter );
			return;
		}

		// 过滤有效 unit
		std::vector<uintptr_t> validUnits;
		validUnits.reserve( unit_count );
		for ( int i = 0; i < unit_count; i++ )
		{
			if ( unitPtrs[i] > 0 )
				validUnits.push_back( unitPtrs[i] );
		}

		if ( validUnits.empty( ) )
		{
			TargetProcess->CloseScatterHandle( hScatter );
			std::lock_guard<std::mutex> lock( g_gameMutex );
			computedData.bIsValid = true;
			g_gameData = computedData;
			return;
		}

		// 第二阶段：scatter read 每个 unit 的所有字段
		TargetProcess->ClearScatterHandle( hScatter );

		const size_t numUnits = validUnits.size( );

		// 为每个 unit 准备读取缓冲区
struct UnitReadBuffer
	{
		vec3_t position;
		vec3_t bbmin;
		vec3_t bbmax;
		matrix3x4_t rotation;
		uint16_t unitState;
		uint8_t team;
		uint8_t reloadTime;
		uintptr_t groundMovement;
		uint32_t flags; // unit + 0x90 起 4 字节（m_UnitFlags1-4），0x800 位疑似可见标志
	};

		std::vector<UnitReadBuffer> buffers( numUnits );

		for ( size_t i = 0; i < numUnits; i++ )
		{
			uintptr_t addr = validUnits[i];
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::position_offset ), &buffers[i].position, sizeof( vec3_t ) );
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::bbmin_offset ), &buffers[i].bbmin, sizeof( vec3_t ) );
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::bbmax_offset ), &buffers[i].bbmax, sizeof( vec3_t ) );
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::rotation_matrix_offset ), &buffers[i].rotation, sizeof( matrix3x4_t ) );
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::unitState_offset ), &buffers[i].unitState, sizeof( uint16_t ) );
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::teamNum_offset ), &buffers[i].team, sizeof( uint8_t ) );
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::visualReload_offset ), &buffers[i].reloadTime, sizeof( uint8_t ) );
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::groundmovement_offset ), &buffers[i].groundMovement, sizeof( uintptr_t ) );
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::unitFlags1_offset ), &buffers[i].flags, sizeof( uint32_t ) );
		}

		if ( !TargetProcess->ExecuteReadScatter( hScatter, 0, true ) )
		{
			TargetProcess->CloseScatterHandle( hScatter );
			return;
		}

		TargetProcess->CloseScatterHandle( hScatter );

		// --- 5. 预计算渲染数据 ---
		const vec3_t local_position = sdk::cLocalPlayer->getLocalUnit( ).getPosition( );
		computedData.localPosition = local_position;

		// 读取本地玩家是否为飞机
		computedData.bLocalIsPlane = sdk::cLocalPlayer->getLocalUnit( ).getInfo( ).isPlane( );

		// 炸弹落点（飞机模式）
		if ( computedData.bLocalIsPlane )
		{
			computedData.bombImpactPoint = sdk::cGame->ballistics->getBombImpactPoint( );
			computedData.bHasBombImpact = true;
		}

		// 弹速（当前武器弹药速度，陆战坦克预测用）
		computedData.ballisticVelocity = sdk::cGame->ballistics->getVelocity( );

		// 构建 SImGuiUnit 列表
		for ( size_t i = 0; i < numUnits; i++ )
		{
			// 跳过无效敌人
			if ( !is_valid_enemy( buffers[i].unitState, buffers[i].team ) )
				continue;

			// 跳过空位置
			if ( buffers[i].position.empty( ) )
				continue;

			SImGuiUnit unit;
			unit.unitAddr = validUnits[i];
			unit.bValidEnemy = true; // 通过 is_valid_enemy 过滤，标记为有效敌人供渲染线程使用

			// 读取载具名字：unit + info_offset(0x1010) -> info + 0x28 -> ReadString
			// 字符串需要两步间接寻址，无法通过 scatter read 批量读取，单独读取
			{
				uintptr_t infoAddr = TargetProcess->Read<uintptr_t>( validUnits[i] + offsets::unit_offsets::info_offset );
				if ( infoAddr )
				{
					uintptr_t nameAddr = TargetProcess->Read<uintptr_t>( infoAddr + 0x28 );
					if ( nameAddr )
					{
						unit.vehicleName = TargetProcess->ReadString( nameAddr );

						// 过滤 UTF-8 零宽空格 (U+200B = E2 80 8B)
						// War Thunder 在中文字符间插入零宽空格，ImGui 会渲染为问号/方块
						{
							const std::string zwsp = std::string( "\xE2\x80\x8B", 3 );
							size_t pos = 0;
							while ( ( pos = unit.vehicleName.find( zwsp, pos ) ) != std::string::npos )
								unit.vehicleName.erase( pos, 3 );
						}

						// 过滤防空炮/火炮：中文客户端单位名为"防空炮"(E9 98 B2 E7 A9 BA)、"火炮"(E7 81 AB E7 82 AE)，陆战/空战均不显示
						// 用字节转义避免源文件编码问题
						if ( unit.vehicleName.find( "\xE9\x98\xB2\xE7\xA9\xBA" ) != std::string::npos )
							continue;
						if ( unit.vehicleName.find( "\xE7\x81\xAB\xE7\x82\xAE" ) != std::string::npos )
							continue;
					}
				}
			}

			unit.worldOrigin = buffers[i].position;
			unit.worldBounds = AABB( buffers[i].bbmin, buffers[i].bbmax );
			unit.rotation = buffers[i].rotation;
			unit.team = buffers[i].team;
			unit.unitState = buffers[i].unitState;
			unit.reloadTime = buffers[i].reloadTime;

			// 目标速度：unit+0x2100 是"地面运动容器"指针（第三方 2.59.0.44 验证），
			// 速度向量在容器内 +0x5C（ground_velocity_offset）。必须二次解引用，不能直接当 vec3 读！
			{
				vec3_t tgtVel;
				if ( buffers[i].groundMovement )
				{
					tgtVel = TargetProcess->Read< vec3_t >( buffers[i].groundMovement + offsets::unit_offsets::ground_velocity_offset );
				}
				else
				{
					// 地面容器无效（可能是飞机）：尝试空中运动容器
					// airContainer(0xD50) + 0x15E4（第三方 air_velocity_offset）
					const uintptr_t airMov = TargetProcess->Read< uintptr_t >( validUnits[i] + offsets::unit_offsets::airContainer_offset );
					if ( airMov )
						tgtVel = TargetProcess->Read< vec3_t >( airMov + 0x15E4 );
				}
				unit.velocity = tgtVel;
			}

			// 距离：unit.distance = 2D 水平；另存 3D 直线距离用于弹道飞行时间
			unit.distance = local_position.dist_to( buffers[i].position );
			unit.distance3d = ( buffers[i].position - local_position ).length( );

			// 可见性检测：flags 0x800 位疑似"可见"标志（第三方验证逻辑）
			// 近距离(<=230m)按第三方惯例保守判定为可见，远处以标志位为准
			unit.unitFlags = buffers[i].flags;
			unit.bVisible = ( unit.distance <= 230.0f ) || ( ( buffers[i].flags & 0x800 ) != 0 );

			// 弹道预测：提前量（目标速度×飞行时间）+ 下坠补偿（0.5*g*t^2）
			// 参考第三方：fTime = dist/弹速；aim = 目标位置 + 目标速度×fTime；y += 0.5*9.81*fTime²
			// 距离：3D 直线距离（比 2D 水平距离更贴近真实弹道飞行长度）
			if ( misc::bBallisticPrediction && computedData.ballisticVelocity > 10.0f )
			{
				const float fDist = ( unit.distance3d > 8.0f ) ? unit.distance3d : unit.distance;
				const float fTime = fDist / computedData.ballisticVelocity;

				// 目标瞄准点：车体包围盒中部（比 worldOrigin 更接近实际命中面）
				vec3_t targetPos = unit.worldOrigin;
				targetPos.y += ( buffers[i].bbmin.y + buffers[i].bbmax.y ) * 0.5f;

				vec3_t aimPoint = targetPos + unit.velocity * fTime;
				aimPoint.y += 0.5f * 9.81f * fTime * fTime;
				unit.aimPoint = aimPoint;
				unit.bHasAimPoint = true;
			}

			// 预计算 8 个世界坐标顶点（用旋转矩阵变换）
			// world_to_screen 延迟到渲染线程做，确保视角矩阵是最新的
			const auto& pos = unit.worldOrigin;
			const auto& bmin = unit.worldBounds.m_min;
			const auto& bmax = unit.worldBounds.m_max;
			const auto& rot = unit.rotation;

			const auto r = rot.right;
			const auto f = rot.forward;
			const auto u = rot.up;

			const vec3_t rx0 = { r.x * bmin.x, r.y * bmin.x, r.z * bmin.x };
			const vec3_t rx1 = { r.x * bmax.x, r.y * bmax.x, r.z * bmax.x };
			const vec3_t fy0 = { f.x * bmin.y, f.y * bmin.y, f.z * bmin.y };
			const vec3_t fy1 = { f.x * bmax.y, f.y * bmax.y, f.z * bmax.y };
			const vec3_t uz0 = { u.x * bmin.z, u.y * bmin.z, u.z * bmin.z };
			const vec3_t uz1 = { u.x * bmax.z, u.y * bmax.z, u.z * bmax.z };

			// 8 个世界坐标顶点存入 worldCorners 数组
			unit.worldCorners[0] = { pos.x + rx0.x + fy0.x + uz0.x, pos.y + rx0.y + fy0.y + uz0.y, pos.z + rx0.z + fy0.z + uz0.z };
			unit.worldCorners[1] = { pos.x + rx1.x + fy0.x + uz0.x, pos.y + rx1.y + fy0.y + uz0.y, pos.z + rx1.z + fy0.z + uz0.z };
			unit.worldCorners[2] = { pos.x + rx0.x + fy1.x + uz0.x, pos.y + rx0.y + fy1.y + uz0.y, pos.z + rx0.z + fy1.z + uz0.z };
			unit.worldCorners[3] = { pos.x + rx1.x + fy1.x + uz0.x, pos.y + rx1.y + fy1.y + uz0.y, pos.z + rx1.z + fy1.z + uz0.z };
			unit.worldCorners[4] = { pos.x + rx0.x + fy0.x + uz1.x, pos.y + rx0.y + fy0.y + uz1.y, pos.z + rx0.z + fy0.z + uz1.z };
			unit.worldCorners[5] = { pos.x + rx1.x + fy0.x + uz1.x, pos.y + rx1.y + fy0.y + uz1.y, pos.z + rx1.z + fy0.z + uz1.z };
			unit.worldCorners[6] = { pos.x + rx0.x + fy1.x + uz1.x, pos.y + rx0.y + fy1.y + uz1.y, pos.z + rx0.z + fy1.z + uz1.z };
			unit.worldCorners[7] = { pos.x + rx1.x + fy1.x + uz1.x, pos.y + rx1.y + fy1.y + uz1.y, pos.z + rx1.z + fy1.z + uz1.z };

				computedData.units.push_back( std::move( unit ) );
		}

		computedData.bIsValid = true;

		// --- 6. 写入共享数据（加锁） ---
		{
			std::lock_guard<std::mutex> lock( g_gameMutex );
			g_gameData = std::move( computedData );
		}

		// 节流日志（5 秒一次）：弹道预测关键数据，方便在 %APPDATA%\war-thunder-data\log\*.txt 中跟踪
		{
			static auto s_lastLog = std::chrono::steady_clock::now( );
			const auto now = std::chrono::steady_clock::now( );
			if ( std::chrono::duration_cast<std::chrono::seconds>( now - s_lastLog ).count( ) >= 5 )
			{
				s_lastLog = now;
				if ( g_gameData.bIsValid && !g_gameData.units.empty( ) )
				{
				const auto& u0 = g_gameData.units.front( );
				const float velMag = u0.velocity.length( );
				const float distUsed = u0.distance3d > 8.0f ? u0.distance3d : u0.distance;
				char distBuf[48];
				snprintf( distBuf, sizeof( distBuf ), "dist=%.1fm", distUsed );
					const float fTimeUsed = ( g_gameData.ballisticVelocity > 10.0f ) ? ( distUsed / g_gameData.ballisticVelocity ) : 0.0f;
					char aimBuf[64];
					if ( u0.bHasAimPoint )
						snprintf( aimBuf, sizeof( aimBuf ), " aim=(%.0f,%.0f,%.0f)", u0.aimPoint.x, u0.aimPoint.y, u0.aimPoint.z );
					else
						aimBuf[0] = '\0';
					char fTimeBuf[48];
					if ( velMag > 0.01f )
						snprintf( fTimeBuf, sizeof( fTimeBuf ), " fTime=%.3fs lead=%.1fm", fTimeUsed, velMag * fTimeUsed );
					else
						fTimeBuf[0] = '\0';
					char dropBuf[48];
					if ( g_gameData.ballisticVelocity > 10.0f )
						snprintf( dropBuf, sizeof( dropBuf ), " drop=%.2fm", 0.5f * 9.81f * fTimeUsed * fTimeUsed );
					else
						dropBuf[0] = '\0';

					if ( !u0.vehicleName.empty( ) )
						LOG( "BallisticPred: bVel=%.1f %s | %s vel=%.2f(m/s)%s%s%s\n",
							g_gameData.ballisticVelocity, distBuf, u0.vehicleName.c_str( ), velMag, aimBuf, fTimeBuf, dropBuf );
					else
						LOG( "BallisticPred: bVel=%.1f %s | <unknown> vel=%.2f(m/s)%s%s%s\n",
							g_gameData.ballisticVelocity, distBuf, velMag, aimBuf, fTimeBuf, dropBuf );
				}
				else
				{
					LOG( "BallisticPred: bVel=%.1f nolocalunit\n", g_gameData.ballisticVelocity );
				}
			}
		}
	}

}
