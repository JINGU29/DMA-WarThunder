#pragma once

#include <mutex>
#include <array>
#include <vector>
#include <cmath>
#include <chrono>
#include <unordered_map>

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
	// flags: unit+0x90 的 4 字节（m_UnitFlags1-4）；unitType: unit+0x8C（0=飞机/无人机, 3=地面载具）
	inline auto is_valid_enemy( uint16_t unitState, uint8_t team, uint32_t flags = 0, uint8_t unitType = 0 ) -> bool
	{
		if ( unitState >= 2 )
			return false;

		if ( team == 0 )
			return false;

		const uint8_t local_team = sdk::cLocalPlayer->getLocalUnit( ).getTeam( );
		if ( team == local_team )
			return false;

		// 陆战残骸/未激活实体过滤（log_2026-10-04_14-17-15 验证）：
		// type=3 载具被击毁后，游戏会在原位生成 flags=0x0844000C 的残骸实体（state=0、位置有效，
		// 能通过其他所有过滤 → 表现为“重生残留框”）；玩家重生前的未激活实体同样带 0x08000000 位。
		// 实体被复用为活体时 flags 会恢复正常（0x0044xxxx），不会永久漏敌。
		// ⚠飞机/无人机(type=0)的 0x08000000 是常态位（F/A-18C 正常显示时就是 0x08441000），不能过滤！
		if ( unitType == 3 && ( flags & 0x08000000u ) )
			return false;

		// 无敌实体过滤（grump 帖 hackedhacker 验证：if (unit.UnitFlags & 4) continue;）：
		// bit2 覆盖三类垃圾实体——残骸(0x...0C)、出生区未出动载具(0x...04)、静止侦察无人机(0x00040004)；
		// 所有活体载具低字节仅为 0x00/0x08，绝不含 bit2（log_2026-10-04_14-35 全量验证）。
		// 这也等价于 grump 说的“护盾(重生保护)时长>15s 过滤”——长期无敌的正是这些实体。
		if ( flags & 0x4u )
			return false;

		// 空中单位残骸/未激活过滤（米-35P 案例）：活体飞机/直升机 flags 低16位必含 0x1000 位
		// （F/A-18C: 0x08441000/0x08441400/0x08441C08；米-35P 活体: 0x08441008/0x08441C08/0x08443C08）；
		// 而直升机残骸(0x0844000C)和出生区未出动载具(0x00440004)均无此位。
		// ⚠仅适用于 type=0：坦克活体可无 0x1000（如 BMP-2 的 0x00440408），不能对 type=3 用！
		if ( unitType == 0 && ( flags & 0x1000u ) == 0 )
			return false;

		// 侦察无人机等虚假实体过滤（log_2026-10-04_14-02 / 14-17 / 14-35 三局验证）：
		// “微型侦察无人机”= 地图固定侦察点（整局静止）+ 敌方玩家放出的侦察无人机消耗品（缓慢移动、无敌），
		// 表现为“对局中不存在却一直显示、全程无敌”。其 flags 为 0x0004xxxx / 0x8004xxxx；
		// 而所有真实可控单位（坦克/飞机/直升机，含未激活/残骸态）flags 第三字节均为 0x44/0x84/0x88
		// 即必含 0x00400000 位 —— 缺此位即无人机类实体，过滤。
		// （grump 帖建议按“护盾(重生保护)时长>15s”过滤，但该字段无现成偏移量；此位规律等价且已双局验证）
		if ( ( flags & 0x00400000u ) == 0 )
			return false;

		// unitType 白名单（grump hackedhacker 过滤系统）：0=飞机/直升机/无人机, 3=地面载具, 5=合法类型；
		// type=8 为地图静态/AI 单位（Phase 0 探测确认），其余未知类型一律过滤
		if ( unitType != 0 && unitType != 3 && unitType != 5 )
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
		uint8_t unitType; // unit + 0x8C（m_UnitType），Phase 0 探测：第三方 0/3/5 过滤依据
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
		TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::unitType_offset ), &buffers[i].unitType, sizeof( uint8_t ) );
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
			// 跳过无效敌人（含 type=3 残骸过滤）
			if ( !is_valid_enemy( buffers[i].unitState, buffers[i].team, buffers[i].flags, buffers[i].unitType ) )
				continue;

			// 跳过空位置
			if ( buffers[i].position.empty( ) )
				continue;

			// 跳过空槽位/回收中的内存（flags 全 F，第三方观察到的瞬变状态）
			if ( buffers[i].flags == 0xFFFFFFFFu )
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

						// 过滤游戏内部测试单位（地图原点的 dummy，会显示为 28km 外的假敌框）
					if ( unit.vehicleName == "dummy" )
						continue;

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
			unit.unitType = buffers[i].unitType;
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

		// --- Phase 0 探测日志 v3（5 秒一次）：unitAddr 身份跟踪 + 消失原因 + 重生标记 ---
		// 1) 显示中敌人：带 addr / 名字 / type / flags / state
		// 2) 消失检测：上一轮显示过、本轮不在显示列表 → 回原始 buffers 输出当前数据与被过滤原因
		// 3) 重生检测：新 addr 出现且名字曾在其他 addr 上出现过 → 标记 RESPAWN?
		{
			static auto s_lastProbe = std::chrono::steady_clock::now( );
			static std::unordered_map<uintptr_t, std::string> s_unitNames;  // addr -> 上次已知名字
			static std::unordered_map<uintptr_t, uint32_t> s_lastFlags;     // addr -> 上次 flags（仅显示过的）
			static std::unordered_map<uintptr_t, std::string> s_goneReason; // addr -> 消失原因（dead/fog/wreck/removed）
			if ( std::chrono::duration_cast<std::chrono::seconds>( std::chrono::steady_clock::now( ) - s_lastProbe ).count( ) >= 5 )
			{
				s_lastProbe = std::chrono::steady_clock::now( );

				std::unordered_map<uintptr_t, bool> curDisplayed;
				LOG( "[Probe] === displayed: %d / raw: %d ===\n",
					static_cast<int>( computedData.units.size( ) ), static_cast<int>( numUnits ) );
				size_t shown = 0;
				for ( const auto& u : computedData.units )
				{
					curDisplayed[u.unitAddr] = true;
					if ( shown++ >= 15 )
					{
						LOG( "[Probe] ... (%d more)\n", static_cast<int>( computedData.units.size( ) - 15 ) );
						break;
					}
					// 重生/迷雾重现/残骸复用检测：新 addr + 名字曾在其他 addr 出现
					// 旧 addr 因 state>=2 消失 → RESPAWN?（玩家重生，游戏重建实体）
					// 旧 addr 因迷雾消失 → FOG-RETURN（服务器恢复下发，实体换地址重建）
					// 新实体本身是无敌态（残骸 bit2 / 未激活）→ WRECK-REUSE（击毁点残骸换地址复现）
					char respawnTag[96] = "";
					if ( s_unitNames.find( u.unitAddr ) == s_unitNames.end( ) && !u.vehicleName.empty( ) )
					{
						const bool newIsWreck = ( u.unitFlags & 0x4u ) != 0
							|| ( u.unitType == 0 && ( u.unitFlags & 0x1000u ) == 0 )
							|| ( u.unitType == 3 && ( u.unitFlags & 0x08000000u ) != 0 );
						if ( newIsWreck )
							snprintf( respawnTag, sizeof( respawnTag ), "  <<< WRECK-REUSE (invincible 0x%08X)", u.unitFlags );
						else
						{
						for ( const auto& kv : s_unitNames )
						{
							if ( kv.first != u.unitAddr && kv.second == u.vehicleName && !curDisplayed.count( kv.first ) )
							{
								const auto gr = s_goneReason.find( kv.first );
								const bool wasDead = ( gr != s_goneReason.end( ) && gr->second.rfind( "dead", 0 ) == 0 );
								snprintf( respawnTag, sizeof( respawnTag ),
									wasDead ? "  <<< RESPAWN? (old %llX dead)" : "  <<< FOG-RETURN (old %llX fogged)",
									static_cast<unsigned long long>( kv.first ) );
								break;
							}
						}
						}
					}
					LOG( "[Probe] %llX %-16s type=%d flags=0x%08X state=%d team=%d %4.0fm pos=(%.0f,%.0f,%.0f)%s\n",
						static_cast<unsigned long long>( u.unitAddr ),
						u.vehicleName.empty( ) ? "<noname>" : u.vehicleName.c_str( ),
						u.unitType, u.unitFlags, u.unitState, u.team, u.distance,
						u.worldOrigin.x, u.worldOrigin.y, u.worldOrigin.z,
						respawnTag );
					if ( !u.vehicleName.empty( ) )
						s_unitNames[u.unitAddr] = u.vehicleName;
				}

				// 消失检测：上轮显示过、本轮不在 → 原始列表中找它，输出被过滤原因
				for ( const auto& kv : s_lastFlags )
				{
					const uintptr_t addr = kv.first;
					if ( curDisplayed.count( addr ) )
						continue;
					UnitReadBuffer* buf = nullptr;
					for ( size_t k = 0; k < numUnits; ++k )
						if ( validUnits[k] == addr ) { buf = &buffers[k]; break; }
					const std::string nm = s_unitNames.count( addr ) ? s_unitNames[addr] : "<unknown>";
					if ( buf )
					{
						const uint8_t localTeam = sdk::cLocalPlayer->getLocalUnit( ).getTeam( );
					char reason[96];
					if ( buf->flags == 0xFFFFFFFFu )
						snprintf( reason, sizeof( reason ), "slot-reset" );
					else if ( buf->unitState >= 2 )
						snprintf( reason, sizeof( reason ), "dead-state=%d", buf->unitState );
					else if ( buf->flags & 0x4u )
						snprintf( reason, sizeof( reason ), "invincible-0x%08X", buf->flags );
					else if ( buf->unitType == 3 && ( buf->flags & 0x08000000u ) )
						snprintf( reason, sizeof( reason ), "wreck-0x%08X", buf->flags );
					else if ( buf->unitType == 0 && ( buf->flags & 0x1000u ) == 0 )
						snprintf( reason, sizeof( reason ), "air-inactive-0x%08X", buf->flags );
					else if ( ( buf->flags & 0x00400000u ) == 0 )
						snprintf( reason, sizeof( reason ), "uav-0x%08X", buf->flags );
					else if ( buf->unitType != 0 && buf->unitType != 3 && buf->unitType != 5 )
						snprintf( reason, sizeof( reason ), "type=%d-skip", buf->unitType );
					else if ( buf->team == 0 || buf->team == localTeam )
						snprintf( reason, sizeof( reason ), "team=%d(local=%d)", buf->team, localTeam );
					else if ( buf->position.empty( ) )
						snprintf( reason, sizeof( reason ), "fog-pos-empty" );
					else
						snprintf( reason, sizeof( reason ), "name-filter" );
					s_goneReason[addr] = reason;
					LOG( "[Probe] GONE %llX %-16s flags=0x%08X(was 0x%08X) type=%d state=%d team=%d pos=(%.0f,%.0f,%.0f) by:%s\n",
							static_cast<unsigned long long>( addr ), nm.c_str( ),
							buf->flags, kv.second, buf->unitType, buf->unitState, buf->team,
							buf->position.x, buf->position.y, buf->position.z, reason );
					}
				else
				{
					s_goneReason[addr] = "removed-from-list";
					LOG( "[Probe] GONE %llX %-16s (removed from unit list)\n",
						static_cast<unsigned long long>( addr ), nm.c_str( ) );
				}
				}

				// s_lastFlags 只保留本轮显示的
				{
					std::unordered_map<uintptr_t, uint32_t> keep;
					for ( const auto& u : computedData.units )
						keep[u.unitAddr] = u.unitFlags;
					s_lastFlags.swap( keep );
				}
				if ( s_unitNames.size( ) > 512 )
				{
					s_unitNames.clear( );
					s_goneReason.clear( );
				}
			}
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
