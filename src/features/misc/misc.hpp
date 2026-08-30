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
#include "..\..\features\aimbot\aimbot.hpp"

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
		VMMDLL_SCATTER_HANDLE hScatter = TargetProcess->CreateScatterHandle( );
		if ( !hScatter )
			return false;

		// 读取 cGame 和 local player
		uintptr_t cGame = 0;
		uintptr_t local = 0;
		if ( !TargetProcess->AddScatterReadRequest( hScatter, uint64_t( baseAddr + offsets::cgame_offset ), &cGame, sizeof( cGame ) )
			|| !TargetProcess->AddScatterReadRequest( hScatter, uint64_t( baseAddr + offsets::localplayer_offset ), &local, sizeof( local ) ) )
		{
			TargetProcess->CloseScatterHandle( hScatter );
			return false;
		}

		if ( !TargetProcess->ExecuteReadScatter( hScatter, 0, true ) )
		{
			TargetProcess->CloseScatterHandle( hScatter );
			return false;
		}

		if ( !cGame || !local )
		{
			TargetProcess->CloseScatterHandle( hScatter );
			return false;
		}

		// 读取 cCamera
		uintptr_t cCamera = 0;
		if ( !TargetProcess->AddScatterReadRequest( hScatter, uint64_t( cGame + offsets::cgame_offsets::camera_offset ), &cCamera, sizeof( cCamera ) )
			|| !TargetProcess->ExecuteReadScatter( hScatter, 0, true ) )
		{
			TargetProcess->CloseScatterHandle( hScatter );
			return false;
		}

		if ( !cCamera )
		{
			TargetProcess->CloseScatterHandle( hScatter );
			return false;
		}

		// 读取 viewMatrix
		matrix4x4_t matrix;
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

	// 对所有 unit 进行全量 scatter read + 预计算
	inline auto GameUpdate( ) -> void
	{
		SGameData computedData;

		// --- 1. 缓存检查：在非存活/非观战状态时重新获取指针 ---
		const auto gui_state = sdk::cLocalPlayer->getGuiState( );
		if ( gui_state != GuiState::ALIVE && gui_state != GuiState::SPEC )
		{
			// 重新初始化缓存的指针
			sdk::cLocalPlayer->reinit( );
			sdk::cGame->reinit( );

			// 数据无效，清空共享数据
			std::lock_guard<std::mutex> lock( g_gameMutex );
			g_gameData.bIsValid = false;
			g_gameData.units.clear( );
			return;
		}

		// --- 2. 获取游戏上下文 (cGame, cCamera, viewMatrix) ---
		SImGuiGame gameCtx;
		if ( !ScatterGame( gameCtx ) )
			return;

		computedData.gameCtx = gameCtx;

		// --- 3. 读取 unit 列表信息 ---
		const int unit_count = sdk::cGame->getUnitCount( );
		if ( !unit_count )
		{
			std::lock_guard<std::mutex> lock( g_gameMutex );
			g_gameData = computedData;
			g_gameData.bIsValid = true;
			return;
		}

		const uintptr_t unit_list_base = sdk::cGame->getUnitList( );
		if ( !unit_list_base )
			return;

		// --- 4. 全量 scatter read: 一次性读取所有 unit 的所有字段 ---

		// 第一阶段：scatter read 所有 unit 指针
		VMMDLL_SCATTER_HANDLE hScatter = TargetProcess->CreateScatterHandle( );
		if ( !hScatter )
			return;

		std::vector<uintptr_t> unitPtrs( unit_count );
		for ( int i = 0; i < unit_count; i++ )
		{
			if ( !TargetProcess->AddScatterReadRequest( hScatter, uint64_t( unit_list_base + 0x8 * i ), &unitPtrs[ i ], sizeof( uintptr_t ) ) )
			{
				TargetProcess->CloseScatterHandle( hScatter );
				return;
			}
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
			if ( unitPtrs[ i ] > 0 )
				validUnits.push_back( unitPtrs[ i ] );
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
		// 清空 scatter handle 重新使用
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
			uintptr_t groundMovement;
			uintptr_t infoPtr;
		};

		std::vector<UnitReadBuffer> buffers( numUnits );

		for ( size_t i = 0; i < numUnits; i++ )
		{
			uintptr_t addr = validUnits[ i ];
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::position_offset ), &buffers[ i ].position, sizeof( vec3_t ) );
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::bbmin_offset ), &buffers[ i ].bbmin, sizeof( vec3_t ) );
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::bbmax_offset ), &buffers[ i ].bbmax, sizeof( vec3_t ) );
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::rotation_matrix_offset ), &buffers[ i ].rotation, sizeof( matrix3x4_t ) );
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::unitState_offset ), &buffers[ i ].unitState, sizeof( uint16_t ) );
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::unitArmyNo_offset ), &buffers[ i ].team, sizeof( uint8_t ) );
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::groundmovement_offset ), &buffers[ i ].groundMovement, sizeof( uintptr_t ) );
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::info_offset ), &buffers[ i ].infoPtr, sizeof( uintptr_t ) );
		}

		if ( !TargetProcess->ExecuteReadScatter( hScatter, 0, true ) )
		{
			TargetProcess->CloseScatterHandle( hScatter );
			return;
		}

		// 第三阶段：scatter read 每个 unit 的 ground velocity
		TargetProcess->ClearScatterHandle( hScatter );

		std::vector<vec3_t> velocities( numUnits );
		bool hasVelocity = false;
		for ( size_t i = 0; i < numUnits; i++ )
		{
			if ( buffers[ i ].groundMovement )
			{
				TargetProcess->AddScatterReadRequest( hScatter, uint64_t( buffers[ i ].groundMovement + offsets::unit_offsets::ground_velocity_offset ), &velocities[ i ], sizeof( vec3_t ) );
				hasVelocity = true;
			}
		}

		if ( hasVelocity )
		{
			TargetProcess->ExecuteReadScatter( hScatter, 0, true );
		}

		TargetProcess->CloseScatterHandle( hScatter );

		// --- 5. 预计算渲染数据 ---
		const vec3_t local_position = sdk::cLocalPlayer->getLocalUnit( ).getPosition( );
		computedData.localPosition = local_position;

		// 检查本地玩家是否为飞机
		computedData.bLocalIsPlane = sdk::cLocalPlayer->getLocalUnit( ).getInfo( ).isPlane( );

		// 读取弹道数据（预缓存，无论 aimbot 是否开启）
		computedData.ballisticMass = sdk::cGame->ballistics->getMass( );
		computedData.ballisticCaliber = sdk::cGame->ballistics->getCaliber( );
		computedData.ballisticVelocity = sdk::cGame->ballistics->getVelocity( );
		computedData.ballisticLength = sdk::cGame->ballistics->getLength( );
		computedData.ballisticMaxDist = sdk::cGame->ballistics->getMaxDistance( );

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
			if ( !is_valid_enemy( buffers[ i ].unitState, buffers[ i ].team ) )
				continue;

			// 跳过空位置
			if ( buffers[ i ].position.empty( ) )
				continue;

			SImGuiUnit unit;
			unit.unitAddr = validUnits[ i ];
			unit.worldOrigin = buffers[ i ].position;
			unit.worldBounds = AABB( buffers[ i ].bbmin, buffers[ i ].bbmax );
			unit.rotation = buffers[ i ].rotation;
			unit.team = buffers[ i ].team;
			unit.unitState = buffers[ i ].unitState;
			unit.bValidEnemy = true;
			unit.distance = local_position.dist_to( buffers[ i ].position );

			// 预计算屏幕坐标
			unit.bOnScreen = g_render->world_to_screen( unit.worldOrigin, unit.screenOrigin, gameCtx.viewMatrix );

			// 预计算 AABB 屏幕顶点
			const vec3_t& pos = unit.worldOrigin;
			const vec3_t& bmin = unit.worldBounds.m_min;
			const vec3_t& bmax = unit.worldBounds.m_max;

			// 计算 8 个世界坐标顶点
			std::array<vec3_t, 8> worldCorners = {
				vec3_t( pos.x + bmin.x, pos.y + bmin.y, pos.z + bmin.z ),
				vec3_t( pos.x + bmax.x, pos.y + bmin.y, pos.z + bmin.z ),
				vec3_t( pos.x + bmin.x, pos.y + bmax.y, pos.z + bmin.z ),
				vec3_t( pos.x + bmax.x, pos.y + bmax.y, pos.z + bmin.z ),
				vec3_t( pos.x + bmin.x, pos.y + bmin.y, pos.z + bmax.z ),
				vec3_t( pos.x + bmax.x, pos.y + bmin.y, pos.z + bmax.z ),
				vec3_t( pos.x + bmin.x, pos.y + bmax.y, pos.z + bmax.z ),
				vec3_t( pos.x + bmax.x, pos.y + bmax.y, pos.z + bmax.z )
			};

			// 转换为屏幕坐标
			for ( int j = 0; j < 8; j++ )
			{
				unit.screenBoxVerts[ j ].bOnScreen = g_render->world_to_screen( worldCorners[ j ], unit.screenBoxVerts[ j ].origin, gameCtx.viewMatrix );
			}

			// aimbot 预计算字段
			unit.velocity = velocities[ i ];
			unit.bHasAimPoint = false;

			// aimbot 弹道预测（仅当开启时）
			if ( bAimbotEnabled && computedData.ballisticVelocity > 0 )
			{
				// 使用预读取的弹道数据
				aimbot::BallisticsData bd;
				bd.mass = computedData.ballisticMass;
				bd.caliber = computedData.ballisticCaliber;
				bd.velocity = computedData.ballisticVelocity;
				bd.length = computedData.ballisticLength;
				bd.max_dist = computedData.ballisticMaxDist;

				static aimbot::BallisticsPrediction pred;
				vec3_t targetPos = unit.worldOrigin;
				targetPos.y += 1.0f + ( unit.distance / 150.0f ) * 0.146f;

				if ( unit.velocity.length( ) > 0.1f )
				{
					unit.aimPoint = pred.PredictInterceptPoint( local_position, targetPos, unit.velocity, bd );
				}
				else
				{
					unit.aimPoint = pred.GetAimPoint( local_position, targetPos, bd );
				}
				unit.bHasAimPoint = true;
			}

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
