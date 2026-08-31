#pragma once

#include <mutex>
#include <array>
#include <vector>

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

		// 检查本地玩家是否为飞机
		computedData.bLocalIsPlane = sdk::cLocalPlayer->getLocalUnit( ).getInfo( ).isPlane( );

		// 炸弹落点（飞机模式）
		if ( computedData.bLocalIsPlane )
		{
			computedData.bombImpactPoint = sdk::cGame->ballistics->getBombImpactPoint( );
			computedData.bHasBombImpact = true;
		}

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
			unit.worldOrigin = buffers[i].position;
			unit.worldBounds = AABB( buffers[i].bbmin, buffers[i].bbmax );
			unit.rotation = buffers[i].rotation;
			unit.team = buffers[i].team;
			unit.unitState = buffers[i].unitState;
			unit.reloadTime = buffers[i].reloadTime;
			// 速度读取已移除（aimbot 关闭时不需要）
			unit.bValidEnemy = true;
			unit.distance = local_position.dist_to( buffers[i].position );

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
	}

}
