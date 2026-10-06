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

	// 弹道落点部位选择（DamageModel 部件盒驱动）：0=车体中心（默认）1=炮闩 2=弹药架 3=乘员
	// 同类部件取离本地玩家最近者（炮线最短、最易击穿）；部件数据未加载时回落车体中心
	inline int ballisticAimPart = 0;

	// 弹丸追踪数据层（在飞真炮弹；链路 docs/弹丸追踪-逆向编年史.md）。
	// 轨迹渲染已按需求移除，默认关闭停止每帧 DMA 读；Phase D 弹道校准需采样时置 true
	inline bool bBulletTracer = false;
	// 弹丸轨迹渲染（Phase C，ESP 侧）：机枪弹幕太密集，按最低速度门限裁剪——
	// 默认 950 m/s 恰好滤掉机枪弹（750~950），保留高初速炮弹（1000+）；
	// 想看低初速榴弹/ATGM 把滑条调低即可。数量上限按距离就近优先，防满屏糊。
	// 用户确认：机枪/炮弹显示不需要 → 默认关（导弹/炸弹走 missile_pass 渲染）
	// 机枪/炮弹轨迹渲染已移除（用户不需要）；数据层保留默认关闭，供 Phase D 校准复用

	// 导弹/炸弹追踪渲染（查询选择器链，offsets::missiles）：敌我分色 + 名字 + 弹道
	inline bool bMissileWarn = true;
	inline bool bMissileOwnOnly = false;  // 只标自己发射的（owner@0x58 tagged 已校准 ★2.59 dump；菜单可关看全部）

	// ── DamageModel 乘员/弹药/油箱/炮闩部件标记（网格束链，2026-10-04 实测；链详见 offsets::damage_model::mesh_bundle 注释）──
	inline bool bPartMarkersCrew = true;   // 菜单开关：乘员部件标记（橙，Style 1 渲染）
	inline bool bPartMarkersAmmo = true;   // 菜单开关：弹药部件标记（红）
	inline bool bPartMarkersFuel = false;  // 菜单开关：油箱部件标记（绿，默认关）
	inline bool bPartMarkersBreech = true; // 菜单开关：炮闩部件标记（黄）；四开关相互独立

	// 部件分类器：按 '_' 分段，否决词优先、乘员优先于弹药；与 tools/orpheus/dm_stylepreview.py 同表
	// 新增类别（引擎等）只需加词表行；三载具样本（坦克/军舰/F-15）分类全对。
	// 油箱实测命名（dm_hits 样本）：fuel_tank_dm / fuel_tank_01_dm / fuel_tank_l|r_NN_dm / fuel_tank_exterior_*_dm → 单段 "fuel" 全覆盖
	// 炮闩实测命名：cannon_breech_dm / cannon_breech_01~05_dm → 单段 "breech" 全覆盖（炮管是 gun_barrel，不收）
	inline auto classify_part( const std::string& name ) -> EPartClass
	{
		static const char* CREW[] = { "pilot", "copilot", "gunner", "driver", "commander", "loader", "crew",
			"sailor", "operator", "radioman", "radio", "navigator", "bombardier", "wso", "gunlayer", "torpedoman" };
		static const char* AMMO[] = { "ammo", "ammunition", "shell", "shells", "powder", "magazine",
			"charges", "round", "rounds", "clip", "clips" };
		static const char* FUEL[] = { "fuel", "fueltank" };
		static const char* BREECH[] = { "breech" };
		// 否决词：陷阱件（操控装置/瞄具/潜望镜/座椅舱盖/无线电台等含乘员弹药词但非本体；
		// pump/pipe/filter/gauge/line 为燃油系统附件非油箱本体）
		static const char* VETO[] = { "controls", "sight", "sights", "panoramic", "periscope", "optic",
			"optics", "view", "seat", "hatch", "station",
			"pump", "pipe", "pipes", "filter", "gauge", "line", "lines" };

		std::string lower = name;
		std::transform( lower.begin( ), lower.end( ), lower.begin( ), ::tolower );

		auto match_seg = []( const std::string& seg, const char** words, size_t n ) -> bool {
			for ( size_t k = 0; k < n; ++k )
				if ( seg == words[k] )
					return true;
			return false;
		};

		// 第一遍：否决词优先
		size_t start = 0;
		while ( start <= lower.size( ) )
		{
			const size_t p = lower.find( '_', start );
			const std::string seg = ( p == std::string::npos ) ? lower.substr( start ) : lower.substr( start, p - start );
			if ( !seg.empty( ) && match_seg( seg, VETO, sizeof( VETO ) / sizeof( VETO[0] ) ) )
				return EPartClass::None;
			if ( p == std::string::npos )
				break;
			start = p + 1;
		}
		// 第二遍：乘员 → 弹药 → 油箱 → 炮闩
		start = 0;
		while ( start <= lower.size( ) )
		{
			const size_t p = lower.find( '_', start );
			const std::string seg = ( p == std::string::npos ) ? lower.substr( start ) : lower.substr( start, p - start );
			if ( !seg.empty( ) )
			{
				if ( match_seg( seg, CREW, sizeof( CREW ) / sizeof( CREW[0] ) ) )
					return EPartClass::Crew;
				if ( match_seg( seg, AMMO, sizeof( AMMO ) / sizeof( AMMO[0] ) ) )
					return EPartClass::Ammo;
				if ( match_seg( seg, FUEL, sizeof( FUEL ) / sizeof( FUEL[0] ) ) )
					return EPartClass::Fuel;
				if ( match_seg( seg, BREECH, sizeof( BREECH ) / sizeof( BREECH[0] ) ) )
					return EPartClass::Breech;
			}
			if ( p == std::string::npos )
				break;
			start = p + 1;
		}
		return EPartClass::None;
	}

	// 网格束缓存条目（静态几何：模型空间部件盒）。仅数据线程（GameUpdate 单线程 32ms 周期）访问，无需加锁
	struct SUnitMesh
	{
		uintptr_t objPtr = 0;					// 网格束 obj 指针（变化 → 重建日志）
		std::vector<SPartBox> boxes;			// 模型空间部件盒（类别+8角）
		std::vector<std::string> names;			// 命中部件名（日志对账用）
		std::vector<std::array<float, 3>> prevT;	// 上次配位 t（探测炮塔转动是否更新配位数组）
		int fetchCount = 0;
		bool logged = false;
		std::chrono::steady_clock::time_point lastFetch{};
		bool valid = false;
	};
	inline std::unordered_map<uintptr_t, SUnitMesh> g_meshCache;
	inline std::unordered_map<uintptr_t, std::pair<vec3_t, std::chrono::steady_clock::time_point>> g_unitPrevPos; // 单位速度帧差分兜底（空中容器链失效时）

	// 读取一个单位的网格束并重建缓存：链 4 跳 + header 块 + (部件表+配位连续读) + 名字池 = 7 次 DMA
	inline auto fetch_unit_mesh( uintptr_t unitAddr, SUnitMesh& mesh ) -> bool
	{
		namespace mb = offsets::damage_model::mesh_bundle;

		const uintptr_t dnet = TargetProcess->Read<uintptr_t>( unitAddr + offsets::unit_offsets::damageModelCont_offset );
		if ( !dnet )
			return false;
		const uintptr_t backref = TargetProcess->Read<uintptr_t>( dnet + mb::backref_off );
		if ( backref != unitAddr )	// 反指校验防误读（本地场景恒成立；联机被改写为网络侧指针）
		{
			// 联机对局实测（2026-10-05）：联机时反指≠unitAddr，但 dnet+0x20 'DNET' 魔数、
			// 下游 obj/hdr 与本地同型车完全一致（如 T-90A loop=290）。故反指不符时以魔数兜底，
			// 魔数也不符才判误读拒绝（可拦截 0xDEADBEEF 毒值与半初始化残槽）。
			uint32_t magic = 0;
			if ( !( dnet > 0x10000
				&& TargetProcess->Read( dnet + mb::dnet_tag_off, &magic, sizeof( magic ) )
				&& magic == mb::dnet_magic ) )
				return false;
		}
		const uintptr_t obj = TargetProcess->Read<uintptr_t>( dnet + mb::obj_off );
		if ( !obj )
			return false;
		const uintptr_t hdr = TargetProcess->Read<uintptr_t>( obj + mb::header_off );
		if ( !hdr )
			return false;

		uint8_t hb[mb::hdr_block_size] = { 0 };
		if ( !TargetProcess->Read( hdr, hb, sizeof( hb ) ) )
			return false;
		const uint32_t rel      = *reinterpret_cast<uint32_t*>( hb + mb::hdr_rel_off );
		const uint32_t loop     = *reinterpret_cast<uint32_t*>( hb + mb::hdr_loop_off );
		const uint32_t namePool = *reinterpret_cast<uint32_t*>( hb + mb::hdr_namepool_off );
		const uint32_t nameSize = *reinterpret_cast<uint32_t*>( hb + mb::hdr_namesize_off );
		if ( loop == 0 || loop > mb::max_parts )
			return false;
		if ( namePool == 0 || nameSize == 0 || nameSize > mb::max_name_size )
			return false;

		// 部件表与配位数组内存连续：[hdr+rel, hdr+rel + loop*(0x40+0x30))，一次读完
		const size_t ptabSz  = static_cast<size_t>( loop ) * mb::part_stride;
		const size_t totalSz = ptabSz + static_cast<size_t>( loop ) * mb::place_stride;
		std::vector<uint8_t> buf( totalSz );
		if ( !TargetProcess->Read( hdr + rel, buf.data( ), totalSz ) )
			return false;
		std::vector<uint8_t> nbuf( nameSize );
		if ( !TargetProcess->Read( hdr + namePool, nbuf.data( ), nameSize ) )
			return false;

		const uint8_t* ptab  = buf.data( );
		const uint8_t* place = buf.data( ) + ptabSz;

		std::vector<SPartBox> boxes;
		std::vector<std::string> names;
		std::vector<std::array<float, 3>> newT( loop );

		for ( uint32_t i = 0; i < loop; ++i )
		{
			const uint8_t* e  = ptab + static_cast<size_t>( i ) * mb::part_stride;
			const float*   pl = reinterpret_cast<const float*>( place + static_cast<size_t>( i ) * mb::place_stride );
			newT[i] = { pl[9], pl[10], pl[11] };

			// 部件名（cstr，手动扫 NUL 截断防越界）
			const uint32_t nameOff = *reinterpret_cast<const uint32_t*>( e + mb::part_name_off );
			if ( nameOff >= nameSize )
				continue;
			const char* np = reinterpret_cast<const char*>( nbuf.data( ) ) + nameOff;
			size_t len = 0;
			while ( len < static_cast<size_t>( nameSize ) - nameOff && np[len] != '\0' )
				++len;
			const std::string nm( np, len );

			const EPartClass cls = classify_part( nm );
			if ( cls == EPartClass::None )
				continue;

			// 局部 AABB → 8 角 → 模型空间（R 行主序 · local + t）；退化盒跳过
			const float* bmin = reinterpret_cast<const float*>( e + mb::part_min_off );
			const float* bmax = reinterpret_cast<const float*>( e + mb::part_max_off );
			if ( !( bmax[0] > bmin[0] && bmax[1] > bmin[1] && bmax[2] > bmin[2] ) )
				continue;

			SPartBox sb;
			sb.cls = cls;
			for ( int c = 0; c < 8; ++c )
			{
				const float lx = ( c & 1 ) ? bmax[0] : bmin[0];
				const float ly = ( c & 2 ) ? bmax[1] : bmin[1];
				const float lz = ( c & 4 ) ? bmax[2] : bmin[2];
				sb.corners[c] = vec3_t{
					pl[0] * lx + pl[1] * ly + pl[2] * lz + pl[9],
					pl[3] * lx + pl[4] * ly + pl[5] * lz + pl[10],
					pl[6] * lx + pl[7] * ly + pl[8] * lz + pl[11]
				};
			}
			boxes.push_back( sb );
			names.push_back( nm );
		}

		// 配位动态探测：与上次 t 对比（炮塔转动若会更新配位数组则有变化）
		float maxTDelta = 0.0f;
		if ( mesh.valid && mesh.prevT.size( ) == newT.size( ) )
		{
			for ( size_t i = 0; i < newT.size( ); ++i )
				for ( int k = 0; k < 3; ++k )
				{
					const float d = newT[i][k] - mesh.prevT[i][k];
					maxTDelta = ( std::max )( maxTDelta, d < 0.0f ? -d : d );
				}
		}

		const bool objChanged = ( mesh.objPtr != obj );
		mesh.objPtr = obj;
		mesh.boxes = std::move( boxes );
		mesh.names = std::move( names );
		mesh.prevT = std::move( newT );
		mesh.fetchCount++;
		mesh.valid = true;
		mesh.lastFetch = std::chrono::steady_clock::now( );
		if ( objChanged )
			mesh.logged = false;

		// 日志：首次/obj 变化时输出分类清单（与 dm_stylepreview 对账）；前 3 次附带配位差值
		if ( !mesh.logged || mesh.fetchCount <= 3 )
		{
			int crewN = 0, ammoN = 0, fuelN = 0, breechN = 0;
			std::string crewList, ammoList, fuelList, breechList;
			for ( size_t k = 0; k < mesh.boxes.size( ); ++k )
			{
				if ( mesh.boxes[k].cls == EPartClass::Crew )
				{
					++crewN;
					crewList += mesh.names[k] + " ";
				}
				else if ( mesh.boxes[k].cls == EPartClass::Ammo )
				{
					++ammoN;
					ammoList += mesh.names[k] + " ";
				}
				else if ( mesh.boxes[k].cls == EPartClass::Fuel )
				{
					++fuelN;
					fuelList += mesh.names[k] + " ";
				}
				else
				{
					++breechN;
					breechList += mesh.names[k] + " ";
				}
			}
			LOG( "[MESH] unit=0x%llX parts=%u dnet=0x%llX obj=0x%llX hdr=0x%llX crew=%d ammo=%d fuel=%d breech=%d maxTdelta=%.4f fetch#%d\n[MESH]   crew: %s\n[MESH]   ammo: %s\n[MESH]   fuel: %s\n[MESH]   breech: %s\n",
				static_cast<unsigned long long>( unitAddr ), loop,
				static_cast<unsigned long long>( dnet ), static_cast<unsigned long long>( obj ), static_cast<unsigned long long>( hdr ),
				crewN, ammoN, fuelN, breechN, maxTDelta, mesh.fetchCount,
				crewList.c_str( ), ammoList.c_str( ), fuelList.c_str( ), breechList.c_str( ) );
			mesh.logged = true;
		}
		else if ( maxTDelta > 0.01f )
		{
			LOG( "[MESH-DYN] unit=0x%llX placement moving maxTdelta=%.4f\n",
				static_cast<unsigned long long>( unitAddr ), maxTDelta );
		}
		return true;
	}

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

	// ── 弹丸追踪数据通道（ECS bullet_component；公式/过滤规则见 docs/弹丸追踪-逆向编年史.md §2.2/2.3/§3.1）──
	// 铁律：记录区请求并入 GameUpdate 既有 scatter 句柄（同一次 Execute，绝不开新句柄）；
	//       只读平坦字段（vel/pos），不做指针追逐；地址先过合理闸。
	namespace bullet_pass
	{
		// 运行时解析一次的静态链（typeId → bullet archetype → compOff；版本内不变，失败 2s 节流重试）
		struct SChain
		{
			bool     valid = false;
			uint16_t typeId = 0;
			uint32_t archIdx = 0;
			uint16_t compOff = 0;
		};
		inline SChain g_chain;
		inline std::chrono::steady_clock::time_point g_chainNextTry{};

		// 记录读取缓冲（scatter 落点：一次 0x1C 字节 = vel@+0x124(12) + pos@+0x130(12) + pad(4)）
		struct SReadBuf
		{
			uint64_t row = 0;   // chunk 内记录序号
			vec3_t   vel;
			vec3_t   pos;
			uint32_t pad = 0;
		};

		inline auto popcnt32( uint32_t v ) -> uint32_t
		{
			uint32_t n = 0;
			while ( v ) { v &= v - 1; ++n; }
			return n;
		}

		// 静态链解析（编年史 2.1 公式①②③）：哈希查 typeId → 掩码定位 archetype → 偏移表取 compOff
		inline auto resolve_chain( ) -> bool
		{
			namespace ob = offsets::bullets;
			auto& ch = g_chain;
			ch.valid = false;

			const uintptr_t em = baseAddr + ob::entity_manager;

			// ① 组件哈希表：整表读一次（0x2000 覆盖 bucket 529 + 探测扩展区），找 comp_hash → typeId
			uintptr_t hashTable = 0;
			uint32_t mask = 0;
			if ( !TargetProcess->Read( em + ob::em_hash_table, &hashTable, sizeof( hashTable ) )
				|| !TargetProcess->Read( em + ob::em_hash_mask, &mask, sizeof( mask ) )
				|| hashTable < 0x10000 || mask == 0 || mask > 0x1000000 )
			{
				TRACE("BP resolve: hash tbl/mask bad tbl=%llx mask=%x",
					(unsigned long long)hashTable, mask);
				return false;
			}

			uint8_t hbuf[ 0x2000 ];
			if ( !TargetProcess->Read( hashTable, hbuf, sizeof( hbuf ) ) )
			{
				TRACE("BP resolve: hash read FAIL tbl=%llx", (unsigned long long)hashTable);
				return false;
			}

			uint16_t typeId = 0xFFFF;
			for ( uint32_t off = 0; off + 0xC <= sizeof( hbuf ); off += 0xC )
			{
				uint32_t h = 0;
				memcpy( &h, hbuf + off + 4, sizeof( h ) );
				if ( h == ob::comp_hash )
				{
					memcpy( &typeId, hbuf + off + 8, sizeof( typeId ) );
					break;
				}
			}
			if ( typeId == 0xFFFF )
			{
				TRACE("BP resolve: hash 0x%x NOT found in table %llx",
					ob::comp_hash, (unsigned long long)hashTable);
				return false;
			}
			TRACE("BP resolve: typeId=%u", typeId);

			// ② archetype 元数据表：compStart≤typeId<compEnd 且掩码位置位（实测全表唯一）
			uintptr_t archMeta = 0;
			if ( !TargetProcess->Read( em + ob::em_arch_meta, &archMeta, sizeof( archMeta ) ) || archMeta < 0x10000 )
				return false;

			uintptr_t prefixTbl = 0, offTbl = 0;
			TargetProcess->Read( em + ob::em_prefix_table, &prefixTbl, sizeof( prefixTbl ) );
			TargetProcess->Read( em + ob::em_offset_table, &offTbl, sizeof( offTbl ) );

			for ( uint32_t ai = 0; ai < 128; ++ai )
			{
				uint8_t meta[ 0x10 ];
				if ( !TargetProcess->Read( archMeta + static_cast<uintptr_t>( ai ) * 0x10, meta, sizeof( meta ) ) )
					break;
				uint64_t maskPtr = 0;
				uint16_t cs = 0, ce = 0;
				memcpy( &maskPtr, meta, sizeof( maskPtr ) );
				memcpy( &cs, meta + 8, sizeof( cs ) );
				memcpy( &ce, meta + 10, sizeof( ce ) );
				if ( maskPtr < 0x10000 || !( cs <= typeId && typeId < ce ) )
					continue;

				const uint32_t ci = typeId - cs;
				uint64_t maskQ = 0;
				if ( !TargetProcess->Read( maskPtr + static_cast<uintptr_t>( ci >> 5 ) * 8, &maskQ, sizeof( maskQ ) ) )
					continue;
				if ( !( ( maskQ >> ( ci & 31u ) ) & 1u ) )
					continue;

				// ③ slot = popcnt(低位) + (掩码qword>>32) + 1 + prefix[arch]；compOff = u16偏移表[slot]
				const uint32_t below = static_cast<uint32_t>( maskQ ) & ( ( 1u << ( ci & 31u ) ) - 1u );
				uint32_t slot = popcnt32( below ) + static_cast<uint32_t>( maskQ >> 32 ) + 1u;
				uint32_t prefix = 0;
				if ( prefixTbl >= 0x10000 )
					TargetProcess->Read( prefixTbl + static_cast<uintptr_t>( ai ) * 4, &prefix, sizeof( prefix ) );
				slot += prefix;

				uint16_t compOff = 0;
				if ( offTbl < 0x10000
					|| !TargetProcess->Read( offTbl + static_cast<uintptr_t>( slot ) * 2, &compOff, sizeof( compOff ) ) )
					continue;
				if ( compOff == 0 || compOff > 0x1000 )
					continue;

				ch.typeId = typeId;
				ch.archIdx = ai;
				ch.compOff = compOff;
				ch.valid = true;
				TRACE("BP resolve OK: typeId=%u arch=%u compOff=%u", typeId, ai, compOff);
				return true;
			}
			TRACE("BP resolve: no arch with bit (typeId=%u)", typeId);
			return false;
		}

		// 阶段二：execute 后过滤（死槽/飞机/弹速闸）并填充 computedData.bullets
		// 三类甄别（编年史 2.3）：死槽=全零/非有限；飞机=行寿命>30s 恒速；真炮弹=短寿命 650~930 m/s
		inline auto collect( std::vector<SReadBuf>& bufs, uint64_t chunkBase, const vec3_t& localPos, SGameData& data ) -> void
		{
			static uint64_t s_chunk = 0;
			static std::unordered_map<uint64_t, std::chrono::steady_clock::time_point> s_rowSeen;
			if ( s_chunk != chunkBase )   // chunk 重分配：行龄作废
			{
				s_chunk = chunkBase;
				s_rowSeen.clear( );
			}
			const auto now = std::chrono::steady_clock::now( );

			for ( const auto& b : bufs )
			{
				const float sp = b.vel.length( );
				const bool finite = std::isfinite( sp ) && std::isfinite( b.pos.x )
					&& std::isfinite( b.pos.y ) && std::isfinite( b.pos.z );
				if ( !finite || sp < 1.0f || sp > 3000.0f
					|| ( b.pos.x == 0.0f && b.pos.y == 0.0f && b.pos.z == 0.0f ) )
				{
					s_rowSeen.erase( b.row );   // 死槽：清行龄，行复用时重新计龄
					continue;
				}

				// 行寿命 >30s = 持续移动的飞机（非弹道），过滤但保留行龄
				auto& seen = s_rowSeen[ b.row ];
				if ( seen == std::chrono::steady_clock::time_point{} )
					seen = now;
				if ( std::chrono::duration_cast<std::chrono::seconds>( now - seen ).count( ) > 30 )
					continue;

				SImGuiBullet bl;
				bl.position = b.pos;
				bl.velocity = b.vel;
				bl.speed = sp;
				bl.distance = ( b.pos - localPos ).length( );
				data.bullets.push_back( std::move( bl ) );
			}
			TRACE("BP collect: in=%zu live=%zu", bufs.size( ), data.bullets.size( ));
		}

		// 读取弹丸记录：独立 scatter 句柄（崩溃教训见编年史 2.5：与单位 scatter 共享句柄触发
		// VMMDLL 内部写越界 → ExecuteReadScatter GS cookie 失败 0xC0000409；弹丸量小(≤256 页)
		// 自建自关，单位 scatter 主链保持零改动）。任何一步失败都静默跳过本帧。
		inline auto execute_pass( SGameData& data, const vec3_t& localPos ) -> void
		{
			namespace ob = offsets::bullets;
			const auto& ch = g_chain;
			if ( !ch.valid )
				return;

			const uintptr_t em = baseAddr + ob::entity_manager;

			// chunk 描述符：EM+0x178 数组按 archetypeIdx 索引，flag@0xF 非零则间接
			uintptr_t descArr = 0;
			if ( !TargetProcess->Read( em + ob::em_chunk_desc, &descArr, sizeof( descArr ) ) || descArr < 0x10000 )
				return;

			uint8_t d0[ 0x20 ];
			uintptr_t desc = descArr + static_cast<uintptr_t>( ch.archIdx ) * 0x20;
			if ( !TargetProcess->Read( desc, d0, sizeof( d0 ) ) )
				return;
			if ( d0[ 0xF ] != 0 )
			{
				uintptr_t ind = 0;
				memcpy( &ind, d0, sizeof( ind ) );
				if ( ind < 0x10000 )
					return;
				desc = ind;
			}

			// chunk 条目（sel=0 恒成立，实测）：+0x00=数据基址 +0x08=活记录数 +0x0C=shift
			uint8_t ce[ 0x10 ];
			if ( !TargetProcess->Read( desc, ce, sizeof( ce ) ) )
			{
				TRACE("BP exec: chunk read FAIL desc=%llx", (unsigned long long)desc);
				return;
			}
			uint64_t chunkBase = 0;
			uint32_t count = 0;
			memcpy( &chunkBase, ce, sizeof( chunkBase ) );
			memcpy( &count, ce + 8, sizeof( count ) );
			const uint8_t shift = ce[ 0xC ];
			TRACE("BP exec: chunk=%llx count=%u shift=%u",
				(unsigned long long)chunkBase, count, shift);
			// 严格校验：活记录数不可能超过 chunk 容量 2^shift；基址过合理闸
			if ( chunkBase < 0x10000 || count == 0 || shift == 0 || shift > ob::max_shift )
				return;
			if ( count > ( 1u << shift ) )
				return;
			if ( count > ob::max_bullets )
				count = ob::max_bullets;
			const uintptr_t dataBase = chunkBase + ( static_cast<uint64_t>( ch.compOff ) << shift );
			if ( dataBase < 0x10000 )
				return;

			// 单次平坦读整个记录区（count ≤ 2^shift，实测 64 槽 chunk → ≤24.5KB）。
			// 不走 scatter——崩溃复盘 2.5：本环境 scatter 执行期 GS cookie 被砸（3/3 确定性复现，
			// 独立句柄仍复现），改为零 scatter 的平坦读彻底绕开 VMMDLL_Scatter 路径。
			const size_t readSz = static_cast<size_t>( count ) * ob::record_size;
			std::vector<uint8_t> rbuf( readSz );
			TRACE("BP exec: flat read %llx cb=%zx", (unsigned long long)dataBase, readSz);
			if ( !TargetProcess->Read( dataBase, rbuf.data( ), readSz ) )
			{
				TRACE("BP exec: flat read FAIL");
				return;
			}
			TRACE("BP exec: flat read ok, filtering");

			std::vector<SReadBuf> bufs( count );
			for ( uint32_t i = 0; i < count; ++i )
			{
				const uint8_t* rec = rbuf.data( ) + static_cast<size_t>( i ) * ob::record_size;
				bufs[ i ].row = i;
				memcpy( &bufs[ i ].vel, rec + ob::tracer_velocity, sizeof( vec3_t ) );
				memcpy( &bufs[ i ].pos, rec + ob::tracer_position, sizeof( vec3_t ) );
			}
			collect( bufs, chunkBase, localPos, data );
		}

	}

	// ── 导弹/炸弹查询链（offsets::missiles；选择器→indexData→子列表→active[i]==1→projectile）──
	// 结构由用户探针实测（2.57 社区逆向同源，与 bullet 链共用 EntityManager）；解析全程 TRACE
	// 可调（布局解释如与实测不符，一轮日志即可校正）。
	namespace missile_pass
	{
		// ── 路线一：ECS 取坐标链（2.59 实测 ★★★，编年史 §2.7.2）──
		// EM → 含 typeId 的 archetype（掩码位图判定）→ 活实体行 row →
		// objptr = u64[ cb + 0x2c0 + row*0x10 ]（指针列 0x10/行）→
		// 世界坐标 = [objptr + POS_OFF] f32 vec3
		// 实测参数：rocket(tid=0x54, arch33, POS_OFF=+0x2C0) / bomb(tid=0x59, arch32, POS_OFF=+0x244)
		// owner：obj+0x58 tagged unit 指针（低位=flag，比较前 &~1）★2.59 msl_obj_dump 校准
		// 速度：obj+POS_OFF+0x1C vec3（★2.59 dump 校准，帧差分方向误差 <1%）
		struct STypeParam
		{
			uint16_t typeId;
			uintptr_t posOff;
			uintptr_t velOff;
			bool isBomb;
		};

		inline std::unordered_map<uintptr_t, std::string> g_mslNameCache;   // obj 指针 → 武器名
		inline std::unordered_map<uintptr_t, std::pair<vec3_t, std::chrono::steady_clock::time_point>> g_mslPrevPos; // 帧差分速度

		// 速度获取：obj+velOff 实测速度（★2.59 dump 校准），兜底帧差分
		inline auto get_vel( uintptr_t objptr, uintptr_t velOff, const vec3_t& pos ) -> vec3_t
		{
			vec3_t vel{};
			// ① obj+velOff（rocket=0x2DC / bomb=0x260；dump 实测 (-110.7,-198.2,-192.0) 与帧差分方向一致）
			vec3_t v{};
			if ( TargetProcess->Read( objptr + velOff, &v, sizeof( v ) ) )
			{
				const float s = v.length( );
				if ( std::isfinite( s ) && s > 0.5f && s < 5000.0f )
					return v;
			}
			// ② 帧差分兜底
			const auto now = std::chrono::steady_clock::now( );
			auto it = g_mslPrevPos.find( objptr );
			if ( it != g_mslPrevPos.end( ) )
			{
				const float dt = std::chrono::duration<float>( now - it->second.second ).count( );
				if ( dt > 0.005f )
				{
					const vec3_t raw = ( pos - it->second.first ) / dt;
					if ( raw.length( ) < 3000.0f )
						vel = raw;
				}
			}
			return vel;
		}

		// 帧差分状态更新（在 pos 读取后调用）
		inline auto update_prev( uintptr_t objptr, const vec3_t& pos ) -> void
		{
			g_mslPrevPos[ objptr ] = { pos, std::chrono::steady_clock::now( ) };
			if ( g_mslPrevPos.size( ) > 256 )
				g_mslPrevPos.clear( );
		}

		// 在 archetype 元数据表中找含 tid 的 archetype，走 slot/偏移链拿 chunk 头
		inline auto resolve_chunk = []( uint16_t tid, uintptr_t& cb, uint32_t& count, uint8_t& shift,
			uint16_t& compOffOut, uint32_t& aiOut ) -> bool
		{
			namespace ob = offsets::bullets;
			const uintptr_t em = baseAddr + ob::entity_manager;
			const uintptr_t archMeta = TargetProcess->Read<uintptr_t>( em + ob::em_arch_meta );
			const uintptr_t prefixTbl = TargetProcess->Read<uintptr_t>( em + ob::em_prefix_table );
			const uintptr_t offTbl = TargetProcess->Read<uintptr_t>( em + ob::em_offset_table );
			if ( archMeta < 0x10000 )
				return false;

			for ( uint32_t ai = 0; ai < 128; ++ai )
			{
				uint8_t meta[ 0x10 ];
				if ( !TargetProcess->Read( archMeta + static_cast<uintptr_t>( ai ) * 0x10, meta, sizeof( meta ) ) )
					break;
				uint64_t maskPtr = 0;
				uint16_t cs = 0, ce = 0;
				memcpy( &maskPtr, meta, sizeof( maskPtr ) );
				memcpy( &cs, meta + 8, sizeof( cs ) );
				memcpy( &ce, meta + 10, sizeof( ce ) );
				if ( maskPtr < 0x10000 || !( cs <= tid && tid < ce ) )
					continue;

				const uint32_t ci = tid - cs;
				uint64_t maskQ = 0;
				if ( !TargetProcess->Read( maskPtr + static_cast<uintptr_t>( ci >> 5 ) * 8, &maskQ, sizeof( maskQ ) ) )
					continue;
				if ( !( ( maskQ >> ( ci & 31u ) ) & 1u ) )
					continue;

				// slot 走链 → compOff → chunk 头（同 bullet 链公式③）
				const uint32_t below = static_cast<uint32_t>( maskQ ) & ( ( 1u << ( ci & 31u ) ) - 1u );
				uint32_t n = below, pc = 0;
				while ( n ) { n &= n - 1; ++pc; }
				uint32_t slot = pc + static_cast<uint32_t>( maskQ >> 32 ) + 1u;
				uint32_t prefix = 0;
				if ( prefixTbl >= 0x10000 )
					TargetProcess->Read( prefixTbl + static_cast<uintptr_t>( ai ) * 4, &prefix, sizeof( prefix ) );
				slot += prefix;

				uint16_t compOff = 0;
				if ( offTbl < 0x10000
					|| !TargetProcess->Read( offTbl + static_cast<uintptr_t>( slot ) * 2, &compOff, sizeof( compOff ) ) )
					continue;

				// chunk 描述符 = [EM+0x178] + ai*0x20（sel0）
				const uintptr_t descArr = TargetProcess->Read<uintptr_t>( em + ob::em_chunk_desc );
				if ( descArr < 0x10000 )
					continue;
				uint8_t d0[ 0x20 ];
				uintptr_t desc = descArr + static_cast<uintptr_t>( ai ) * 0x20;
				if ( !TargetProcess->Read( desc, d0, sizeof( d0 ) ) )
					continue;
				if ( d0[ 0xF ] != 0 )
				{
					uintptr_t ind = 0;
					memcpy( &ind, d0, sizeof( ind ) );
					if ( ind < 0x10000 )
						continue;
					desc = ind;
				}
				uint8_t cent[ 0x10 ];
				if ( !TargetProcess->Read( desc, cent, sizeof( cent ) ) )
					continue;
				memcpy( &cb, cent, sizeof( cb ) );
				memcpy( &count, cent + 8, sizeof( count ) );
				shift = cent[ 0xC ];
				compOffOut = compOff;
				aiOut = ai;
				return cb > 0x10000 && count > 0 && shift > 0 && shift <= 12;
			}
			return false;
		};

		// 主入口：双类型枚举 → owner 过滤 → data.missiles
		inline auto execute_pass( SGameData& data, const vec3_t& localPos, uintptr_t myUnit,
			const std::unordered_map< int16_t, uintptr_t >& unitIdxMap ) -> void
		{
			namespace om = offsets::missiles;
			data.missiles.clear( );
			data.bHasRocketImpact = false;

			// ★2026-10-06 回滚记录：曾误用 bullet 组件哈希(0xBC84D211，offsets 注释本就写明
			// "bullet_component 描述哈希")动态解析"导弹 tid"→解析出子弹的 0x58→导弹链全灭。
			// 实测 0x54(rocket)/0x59(bomb) 跨战局稳定（12:03 与 13:0x 两场均正常检出），保持硬编码。
			const STypeParam types[ 2 ] = {
				{ 0x54, 0x2C0, 0x2DC, false },   // rocket：POS_OFF=+0x2C0，vel=+0x2DC ★dump 校准
				{ 0x59, 0x244, 0x260, true },    // bomb：POS_OFF=+0x244，vel=pos+0x1C 推定
			};

			const int16_t myUnitIndex = ( myUnit > 0x10000 )
				? TargetProcess->Read<int16_t>( myUnit + offsets::unit_offsets::unitIndex_offset ) : -1;

			for ( const auto& tp : types )
			{
				uintptr_t cb = 0;
				uint32_t count = 0;
				uint8_t shift = 0;
				uint16_t compOff = 0;
				uint32_t aidx = 0;
				if ( !resolve_chunk( tp.typeId, cb, count, shift, compOff, aidx ) )
					continue;

				// （2026-10-06 清理：链诊断 dump 块已移除——校准任务闭环，避免首弹瞬间数百次
				//  TRACE 文件开关造成的数据线程卡顿。如需重新校准，用 tools/orpheus 探针脚本。）
				if ( count > om::max_missiles )
					count = om::max_missiles;

				// 指针列一次平坦读：cb+0x2c0 起，每行 0x10 字节取首个 qword
				const uintptr_t ptrCol = cb + 0x2C0;
				const size_t readSz = static_cast<size_t>( count ) * 0x10;
				std::vector<uint8_t> pbuf( readSz );
				if ( !TargetProcess->Read( ptrCol, pbuf.data( ), readSz ) )
					continue;

				for ( uint32_t row = 0; row < count; ++row )
				{
					uint64_t objptr = 0;
					memcpy( &objptr, pbuf.data( ) + static_cast<size_t>( row ) * 0x10, sizeof( objptr ) );
					if ( objptr < 0x10000 || objptr > 0x7FFFFFFFFFFF )
						continue;

					vec3_t pos{};
					if ( !TargetProcess->Read( objptr + tp.posOff, &pos, sizeof( pos ) ) )
						continue;
					if ( !std::isfinite( pos.x ) || !std::isfinite( pos.y ) || !std::isfinite( pos.z ) )
						continue;
					if ( pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f )
						continue;
					if ( fabs( pos.x ) > 60000.0f || fabs( pos.y ) > 60000.0f || fabs( pos.z ) > 60000.0f )
						continue;

					// owner：obj+0x58 tagged 指针 ★2.59 dump 校准：off=0x58 q=2a6d303f421 == myUnit|1
					// （0x48 是 u32 对 (3, 0x66474) 非指针；0x4A0 是同指针第二份拷贝）
					uint64_t ownerRaw = 0;
					TargetProcess->Read( objptr + om::proj_owner, &ownerRaw, sizeof( ownerRaw ) );
					const uint64_t owner = ownerRaw & ~1ULL;
					const bool own = ( owner != 0 && owner == myUnit );
					static int s_ownerLogCount = 0;
					if ( s_ownerLogCount < 8 )
					{
						TRACE("MSL tid=%x row=%u obj=%llx owner58=%llx raw=%llx myUnit=%llx own=%d pos=(%.0f,%.0f,%.0f)",
							tp.typeId, row, (unsigned long long)objptr, (unsigned long long)owner,
							(unsigned long long)ownerRaw,
							(unsigned long long)myUnit, (int)own, pos.x, pos.y, pos.z);
						++s_ownerLogCount;
					}

					// （2026-10-06 清理：校准 dump 块已移除——msl_obj_dump/CAL、bc_ccip_dump/CCIP、
					//  GUID 探针均已完成使命（owner/pos/vel 校准闭环、CCIP 0x1C9C 与 0x648 实测否定），
					//  避免首枚导弹出现瞬间 0x2000 DMA 读 + 数百行 TRACE 文件开关造成卡顿。
					//  重新校准时用 tools/orpheus 探针脚本，见 docs/弹丸追踪-逆向编年史.md。）
					// 方案 A：只标自己发射的（菜单可关看全部）
					if ( misc::bMissileOwnOnly && !own )
						continue;

					SImGuiMissile m;
					m.position = pos;
					m.speed = 0.0f;
					m.velocity = get_vel( objptr, tp.velOff, pos );   // obj+velOff 实测速度（帧差分兜底）
					update_prev( objptr, pos );
					m.sampleTime = std::chrono::steady_clock::now( );
					m.speed = m.velocity.length( );
					m.distance = ( pos - localPos ).length( );
					m.isBomb = tp.isBomb;
					m.own = own;
					m.projAddr = objptr;

					// 名字：尝试 NameCont 链（若 ECS obj 与 Projectile 同体则命中，否则空名降级）
					auto nit = g_mslNameCache.find( objptr );
					if ( nit == g_mslNameCache.end( ) )
					{
						const uintptr_t ncrva = tp.isBomb ? om::proj_namecont_bomb : om::proj_namecont_ms;
						const uintptr_t txrva = tp.isBomb ? om::namecont_text_bomb : om::namecont_text_ms;
						const uintptr_t nc = TargetProcess->Read<uintptr_t>( objptr + ncrva );
						std::string nm;
						if ( nc > 0x10000 )
							nm = TargetProcess->ReadString( nc + txrva, 96 );
						nit = g_mslNameCache.emplace( objptr, std::move( nm ) ).first;
					}
					m.name = nit->second;

					data.missiles.push_back( std::move( m ) );
				}
			}

			if ( g_mslNameCache.size( ) > 512 )
				g_mslNameCache.clear( );

				// 导弹 CCIP 落点：ballistics 容器 +0x1C9C（★2.59 实测否定——该区域为常量表无世界坐标；
				// 此处保留读取但过滤全零/原地值，防止 (0,0,0) 在世界原点误画 pip。正确偏移待弹体 obj 高位 dump 校准）
				if ( data.bLocalIsPlane )
				{
					const uintptr_t bc = TargetProcess->Read<uintptr_t>(
						data.gameCtx.cGame + offsets::cgame_offsets::ballistic_offsets::ballistics_ptr );
					if ( bc > 0x10000 )
					{
						vec3_t imp{};
						if ( TargetProcess->Read( bc + offsets::missiles::rocket_impact_point, &imp, sizeof( imp ) )
							&& std::isfinite( imp.x ) && std::isfinite( imp.y ) && std::isfinite( imp.z )
							&& fabs( imp.x ) < 50000.0f && fabs( imp.y ) < 50000.0f && fabs( imp.z ) < 50000.0f
							&& ( imp.x != 0.0f || imp.y != 0.0f || imp.z != 0.0f ) )
						{
							data.rocketImpactPoint = imp;
							data.bHasRocketImpact = true;
						}
					}
				}
		}
	}

	// ── 幽灵缓存（战争迷雾记忆列表；方案见 docs/ai-offset-hunter-需求累积.md 第15轮定稿）──
	// 语义：确认过的敌人（露过面）即入列，活跃期间每周期刷新；只有死亡判定成功才出列
	//（+战斗结束/场景切换清空防护）。消失后 15 秒窗口内由渲染端画幽灵盒（滑 2 秒 → 钉住）。
	// 同人认定：玩家名（链 unit+0xFA0 → +0x60，monkrel API 2.59.0.46）——跨地址重现时
	// 按名字迁移条目实现无缝重连；玩家名只在活跃期读（首读缓存，空则 5s 重试）。
	// 崩溃安全：探测只对“已从列表消失”的旧地址做同步平读（≤32 条/周期、平坦字段不追
	// 指针、读失败保留），不并入主 scatter；探测期不重读名字。
	struct SGhostEntry
	{
		uintptr_t addr = 0;
		std::array<vec3_t, 8> corners{};
		vec3_t velocity{};
		std::string dispName;      // 型号名（活跃期随 vehicleName 更新；空值不覆盖旧名）
		std::string playerName;    // 玩家名（同人匹配键）
		std::chrono::steady_clock::time_point lastSeen{};     // 最后一次活跃刷新（==unitScatterTime 判活跃）
		std::chrono::steady_clock::time_point lastPNameTry{}; // 玩家名重试节流
	};
	inline std::vector<SGhostEntry> g_ghostList;
	inline uintptr_t g_ghostListBase = 0;   // 场景防护：unit 列表基址（变化 = 场景切换 → 清空）
	constexpr size_t kGhostMax = 128;       // 容量上限（满淘汰最旧）
	constexpr int    kGhostProbeMax = 32;   // 同步平读探测上限（条/周期）

	// 死特征判定：state>=2 或（地面单位残骸位 0x08000000；被过滤实体与探测共用）
	inline auto ghost_is_dead_feature( uint16_t state, uint32_t flags, uint8_t type ) -> bool
	{
		return state >= 2 || ( type == 3 && ( flags & 0x08000000u ) != 0 );
	}

	// 玩家名链读取（仅活跃地址调用）：unit+0xFA0(m_PlayerInfo) → +0x60(m_Name) → 字符串
	// 自校：本地玩家名读出应=本人游戏名（见 GameUpdate [GHOST] self-name 打点）
	inline auto ghost_read_player_name( uintptr_t unitAddr ) -> std::string
	{
		const uintptr_t pinfo = TargetProcess->Read<uintptr_t>( unitAddr + offsets::unit_offsets::playerInfo_offset );
		if ( pinfo <= 0x10000 )
			return {};
		const uintptr_t nptr = TargetProcess->Read<uintptr_t>( pinfo + offsets::localplayer::name_offset );
		if ( nptr <= 0x10000 )
			return {};
		std::string nm = TargetProcess->ReadString( nptr, 96 );
		// 零宽空格过滤（同型号名链规则）
		const std::string zwsp = std::string( "\xE2\x80\x8B", 3 );
		size_t pos = 0;
		while ( ( pos = nm.find( zwsp, pos ) ) != std::string::npos )
			nm.erase( pos, 3 );
		return nm;
	}

	// 零成本判死：被过滤实体（含迷雾态）命中死特征且 addr 在表 → 出列
	inline auto ghost_check_dead_perfilter( uintptr_t addr, uint16_t state, uint32_t flags, uint8_t type ) -> void
	{
		if ( !ghost_is_dead_feature( state, flags, type ) )
			return;
		for ( auto it = g_ghostList.begin( ); it != g_ghostList.end( ); )
		{
			if ( it->addr == addr )
			{
				LOG( "[GHOST] dead(perfilter) addr=%llX st=%u fl=0x%08X ty=%u name=%s pname=%s\n",
					(unsigned long long)addr, state, flags, type, it->dispName.c_str( ), it->playerName.c_str( ) );
				it = g_ghostList.erase( it );
			}
			else
				++it;
		}
	}

	// upsert：活跃实体（通过全部过滤）入列/刷新；同人迁移（玩家名同且旧条目非活跃）
	inline auto ghost_upsert( const SImGuiUnit& unit, std::chrono::steady_clock::time_point tScatter ) -> void
	{
		SGhostEntry* e = nullptr;
		for ( auto& g : g_ghostList )
			if ( g.addr == unit.unitAddr ) { e = &g; break; }

		if ( !e )
		{
			// 同人迁移：仅新建/迁移路径读玩家名（活跃刷新期零开销）
			const std::string pname = ghost_read_player_name( unit.unitAddr );
			if ( !pname.empty( ) )
			{
				for ( auto& g : g_ghostList )
				{
					if ( g.playerName == pname && g.lastSeen != tScatter )
					{
						g.addr = unit.unitAddr;   // 迷雾换地址重建 → 条目迁移（无缝重连）
						e = &g;
						LOG( "[GHOST] migrate addr=%llX pname=%s\n",
							(unsigned long long)unit.unitAddr, pname.c_str( ) );
						break;
					}
				}
			}
			if ( !e )
			{
				if ( g_ghostList.size( ) >= kGhostMax )
				{
					// 满：淘汰 lastSeen 最旧者
					auto oldest = g_ghostList.begin( );
					for ( auto it = g_ghostList.begin( ); it != g_ghostList.end( ); ++it )
						if ( it->lastSeen < oldest->lastSeen )
							oldest = it;
					g_ghostList.erase( oldest );
				}
				SGhostEntry ne;
				ne.addr = unit.unitAddr;
				ne.playerName = pname;
				ne.lastPNameTry = tScatter;
				g_ghostList.push_back( std::move( ne ) );
				e = &g_ghostList.back( );
				LOG( "[GHOST] new addr=%llX name=%s pname=%s\n",
					(unsigned long long)unit.unitAddr, unit.vehicleName.c_str( ), pname.c_str( ) );
			}
			else
				e->playerName = pname;    // 迁移：写入名字（pname 必非空）
		}
		else if ( e->playerName.empty( )
			&& std::chrono::duration_cast<std::chrono::milliseconds>( tScatter - e->lastPNameTry ).count( ) >= 5000 )
		{
			// 活跃期玩家名补读（5s 节流；只对活跃地址读——红线）
			e->lastPNameTry = tScatter;
			e->playerName = ghost_read_player_name( unit.unitAddr );
		}

		e->corners = unit.worldCorners;
		e->velocity = unit.velocity;
		if ( !unit.vehicleName.empty( ) )
			e->dispName = unit.vehicleName;
		e->lastSeen = tScatter;
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
			g_gameData.bullets.clear( );
			g_gameData.ghosts.clear( );
			// 幽灵记忆列表随战斗结束清空（下局 unit 列表基址必然变化，双保险）
			g_ghostList.clear( );
			return;
		}

		// 进入战斗状态，记录日志
		if ( state_changed )
		{
			LOG( "Entered battle! (gui_state=%d)\n", gui_state );
			last_debug_log = now;
		}

		// --- 2. 获取游戏上下文 (cGame, cCamera, viewMatrix) ---
		TRACE("GU enter state=%d", (int)gui_state);
		SImGuiGame gameCtx;
		if ( !ScatterGame( gameCtx ) )
		{
			TRACE("GU ScatterGame FAIL");
			return;
		}
		TRACE("GU ctx ok cGame=%llx cCam=%llx",
			(unsigned long long)gameCtx.cGame, (unsigned long long)gameCtx.cCamera);

		computedData.gameCtx = gameCtx;

		// --- 3. 读取 unit 列表信息 ---
		const int unit_count = sdk::cGame->getUnitCount( );
		if ( !unit_count || unit_count > 10000 )
		{
			TRACE("GU count invalid=%d", unit_count);
			std::lock_guard<std::mutex> lock( g_gameMutex );
			g_gameData = computedData;
			g_gameData.bIsValid = true;
			return;
		}

		const uintptr_t unit_list_base = sdk::cGame->getUnitList( );
		if ( !unit_list_base )
		{
			TRACE("GU unit_list_base NULL");
			return;
		}
		TRACE("GU count=%d list=%llx", unit_count, (unsigned long long)unit_list_base);

		// 幽灵缓存场景防护：unit 列表基址变化 = 场景切换（新战局/换图）→ 清空记忆
		if ( unit_list_base != g_ghostListBase )
		{
			if ( g_ghostListBase != 0 && !g_ghostList.empty( ) )
				LOG( "[GHOST] list_base change %llX -> %llX, clear %d entries\n",
					(unsigned long long)g_ghostListBase, (unsigned long long)unit_list_base, (int)g_ghostList.size( ) );
			g_ghostListBase = unit_list_base;
			g_ghostList.clear( );
		}

		// [GHOST] 玩家名链自校（节流复检：最多 12 次、5s 间隔、读出名字后停止）：本地玩家名应=本人游戏名；桥 unit+0xFA0 应回到同一 player 对象
		{
			static int s_ghostSelfTries = 0;
			static auto s_ghostSelfNext = std::chrono::steady_clock::now( );
			if ( s_ghostSelfTries < 12 && now >= s_ghostSelfNext )
			{
				s_ghostSelfNext = now + std::chrono::seconds( 5 );
				++s_ghostSelfTries;
				const uintptr_t lp = TargetProcess->Read<uintptr_t>( baseAddr + offsets::globals::local_player );
				if ( lp > 0x10000 )
				{
					const uintptr_t nptr = TargetProcess->Read<uintptr_t>( lp + offsets::localplayer::name_offset );
					std::string selfName;
					if ( nptr > 0x10000 )
						selfName = TargetProcess->ReadString( nptr, 96 );
					// 诊断（首测 name='' 空）：dump lp+0x60 与 nptr 各 16 字节，判定布局（指针/内联字符串/UTF-16）
					char hexA[ 64 ] = ""; char hexB[ 64 ] = "";
					if ( selfName.empty( ) )
					{
						uint8_t dbuf[ 16 ] = { };
						if ( TargetProcess->Read( lp + offsets::localplayer::name_offset, dbuf, sizeof( dbuf ) ) )
							for ( int i = 0; i < 16; ++i ) snprintf( hexA + i * 3, 4, "%02X ", dbuf[ i ] );
						if ( nptr > 0x10000 )
						{
							for ( int i = 0; i < 16; ++i ) dbuf[ i ] = 0;
							if ( TargetProcess->Read( nptr, dbuf, sizeof( dbuf ) ) )
								for ( int i = 0; i < 16; ++i ) snprintf( hexB + i * 3, 4, "%02X ", dbuf[ i ] );
						}
					}
					const uintptr_t myUnitChk = TargetProcess->Read<uintptr_t>( baseAddr + offsets::globals::my_unit );
					const uintptr_t bridge = ( myUnitChk > 0x10000 )
						? TargetProcess->Read<uintptr_t>( myUnitChk + offsets::unit_offsets::playerInfo_offset ) : 0;
					LOG( "[GHOST] self-name check: try=%d lp=%llX nptr=%llX name='%s' heap60=[%s] str=[%s] bridge=%llX same=%d\n",
						s_ghostSelfTries, (unsigned long long)lp, (unsigned long long)nptr, selfName.c_str( ), hexA, hexB,
						(unsigned long long)bridge, (int)( bridge == lp ) );
					if ( !selfName.empty( ) )
						s_ghostSelfTries = 12;   // 成功 → 停止复检
				}
			}
		}

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

		TRACE("GU p1 exec units=%d", unit_count);
		if ( !TargetProcess->ExecuteReadScatter( hScatter, 0, true ) )
		{
			TRACE("GU p1 FAIL");
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
			TRACE("GU valid empty");
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
		uint32_t flags; // unit + 0x90 起 4 字节（m_UnitFlags1-4），0x800 位疑似可见标志
		uint8_t unitType; // unit + 0x8C（m_UnitType），Phase 0 探测：第三方 0/3/5 过滤依据
		int16_t unitIndex; // unit + 0x8（int16）——导弹制导目标反查键
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
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::unitFlags1_offset ), &buffers[i].flags, sizeof( uint32_t ) );
			TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::unitIndex_offset ), &buffers[i].unitIndex, sizeof( int16_t ) );
		TargetProcess->AddScatterReadRequest( hScatter, uint64_t( addr + offsets::unit_offsets::unitType_offset ), &buffers[i].unitType, sizeof( uint8_t ) );
		}

		TRACE("GU p2 exec units=%zu (reqs=%zu)", numUnits, numUnits * 9);
		// ★位置采样基准：scatter 执行区间中点（无偏）。渲染端外推 age = renderNow − sampleTime
		//   以此刻为基；此前 sampleTime 打在单位循环速度块（循环前还有弹丸/导弹/名字链等逐单位同步读），
		//   靠后单位打戳晚 0.5~1s → age 被少算 → 框恒定落后 v×δ（150m/s×0.6s≈90m，2026-10-05 修复）
		const auto tScatterBegin = std::chrono::steady_clock::now( );
		const bool bScatterExecOk = TargetProcess->ExecuteReadScatter( hScatter, 0, true );
		const auto unitScatterTime = tScatterBegin + ( std::chrono::steady_clock::now( ) - tScatterBegin ) / 2;
		if ( !bScatterExecOk )
		{
			TRACE("GU p2 FAIL");
			TargetProcess->CloseScatterHandle( hScatter );
			return;
		}
		TRACE("GU p2 done");

		TargetProcess->CloseScatterHandle( hScatter );

		// --- 5. 预计算渲染数据 ---
		const vec3_t local_position = sdk::cLocalPlayer->getLocalUnit( ).getPosition( );
		computedData.localPosition = local_position;

		// 弹丸追踪：独立流程（链解析 2s 节流重试 → chunk 头校验 → 平坦读记录 → 过滤）
		if ( bBulletTracer )
		{
			auto& chain = bullet_pass::g_chain;
			const auto nowB = std::chrono::steady_clock::now( );
			TRACE("GU bullet: enter valid=%d", (int)chain.valid);
			if ( !chain.valid && nowB >= bullet_pass::g_chainNextTry )
			{
				bullet_pass::g_chainNextTry = nowB + std::chrono::seconds( 2 );
				if ( bullet_pass::resolve_chain( ) )
					LOG( "[BULLET] chain resolved: typeId=%u arch=%u compOff=0x%X\n",
						chain.typeId, chain.archIdx, chain.compOff );
			}
			if ( chain.valid )
				bullet_pass::execute_pass( computedData, local_position );
			TRACE("GU bullet: done live=%zu", computedData.bullets.size( ));
		}

		// 导弹/炸弹查询链 + 导弹 CCIP 落点（offsets::missiles；选择器→子列表→active[i]→projectile）
		const uintptr_t myUnit = TargetProcess->Read<uintptr_t>( baseAddr + offsets::globals::my_unit );
		{
			// 全量单位索引映射（含队友/本地）：导弹制导 TargetUnitId 反查用
			std::unordered_map< int16_t, uintptr_t > unitIdxMap;
			for ( size_t k = 0; k < validUnits.size( ); ++k )
				unitIdxMap[ buffers[ k ].unitIndex ] = validUnits[ k ];
			missile_pass::execute_pass( computedData, local_position, myUnit, unitIdxMap );
		}

		// 本地玩家是否为空中单位：unit+0x8C 是 u8 类型枚举（0=空中/直升机，3=地面，5=其他）。
		// ★修复：旧 isPlane() 把该字节当字符串指针解引用，2.59 上恒 false →
		//   飞机模式判定失效（地面载具照画全套 ESP、预测红框照出），与数据线程 unitType 同源
		{
			const uint8_t localType = ( myUnit > 0x10000 )
				? TargetProcess->Read<uint8_t>( myUnit + offsets::unit_offsets::unitType_offset ) : 3;
			computedData.bLocalIsPlane = ( localType == 0 );
		}

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
			// 幽灵：死特征零成本判死（被过滤/迷雾态也覆盖：state>=2 或地面残骸位 → 出列）
			ghost_check_dead_perfilter( validUnits[i], buffers[i].unitState, buffers[i].flags, buffers[i].unitType );

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
			unit.unitIndex = buffers[i].unitIndex;
			unit.reloadTime = buffers[i].reloadTime;

			// 目标速度：★2.59.0.46 重校准（2026-10-06 Orpheus 差分实测）。旧链
			// unit+0x2100(→+0x5C) / 0xD50(→+0x15E4) 全死（0x2100 现为乱码、+0x15E4 读 (0,0,±1.2)）。
			// 新链：unit+0x1158 / unit+0x12B8 两份同步容器拷贝 → vec3 @ 容器+0x104（+0x134 副本）。
			// 合格线 5~1500 m/s，不合格自动走帧差分兜底（地面坦克速度未单独验证，兜底覆盖）。
			{
				vec3_t tgtVel{};
				auto tryContainer = [&]( uintptr_t contOff ) -> vec3_t {
					const uintptr_t cont = TargetProcess->Read< uintptr_t >( validUnits[i] + contOff );
					if ( cont < 0x10000 || cont > 0x7FFFFFFFFFFF )
						return {};
					const vec3_t v = TargetProcess->Read< vec3_t >( cont + offsets::unit_offsets::container_velocity );
					const float s = v.length( );
					if ( !std::isfinite( s ) || s < 5.0f || s >= 1500.0f )
						return {};
					return v;
				};
				tgtVel = tryContainer( offsets::unit_offsets::vel_container_a );
				if ( tgtVel.length( ) < 0.5f )
					tgtVel = tryContainer( offsets::unit_offsets::vel_container_b );
				// 旧空中容器链兜底（2.59.0.44 及以下仍有效的场合）
				if ( tgtVel.length( ) < 0.5f && unit.unitType == 0 )
				{
					const uintptr_t airMov = TargetProcess->Read< uintptr_t >( validUnits[i] + offsets::unit_offsets::airContainer_offset );
					if ( airMov > 0x10000 && airMov < 0x7FFFFFFFFFFF )
					{
						const vec3_t airVel = TargetProcess->Read< vec3_t >( airMov + 0x15E4 );
						const float s = airVel.length( );
						if ( std::isfinite( s ) && s >= 5.0f && s < 1500.0f )
							tgtVel = airVel;
					}
				}

				// ③ 帧差分兜底：容器速度失效（0 或被清零）时用上一帧位置差算速度
				// ★时间基准（2026-10-05 修复）：位置在 p2 scatter 执行时刻采样，帧差分 dt / prev 表 /
				//   sampleTime 统一改用 unitScatterTime；velNow 仅留给 δ 打点
				const auto velNow = std::chrono::steady_clock::now( );
				if ( tgtVel.length( ) < 0.5f )
				{
					auto it = g_unitPrevPos.find( validUnits[i] );
					if ( it != g_unitPrevPos.end( ) )
					{
						const float dt = std::chrono::duration<float>( unitScatterTime - it->second.second ).count( );
						if ( dt > 0.01f )
						{
							const vec3_t raw = ( buffers[i].position - it->second.first ) / dt;
							const float rawSpd = raw.length( );
							if ( std::isfinite( rawSpd ) && rawSpd < 1500.0f )
								tgtVel = raw;
						}
					}
				}
				g_unitPrevPos[ validUnits[i] ] = { buffers[i].position, unitScatterTime };
				if ( g_unitPrevPos.size( ) > 512 )
					g_unitPrevPos.clear( );

				// 诊断：空中单位最终速度（兜底之后，前 16 条/进程——确认外推拿到的真实速度）
				static int s_airVelLog = 0;
				if ( unit.unitType == 0 && s_airVelLog < 16 )
				{
					TRACE( "AIRVEL unit=%llx vel=(%.1f,%.1f,%.1f) spd=%.1f",
						(unsigned long long)validUnits[i],
						tgtVel.x, tgtVel.y, tgtVel.z, tgtVel.length( ) );
					++s_airVelLog;
				}

				unit.velocity = tgtVel;

				// δ 量化打点（前 12 条/进程）：δ = 循环打戳时刻 − scatter 采样时刻
				//   = 修复前「年龄少算量」→ 恒定落后距离 ≈ δ×速度（进战局用 crash_trace.log 验收）
				static int s_scatDeltaLog = 0;
				if ( s_scatDeltaLog < 12 )
				{
					const float dsec = std::chrono::duration<float>( velNow - unitScatterTime ).count( );
					TRACE( "SCATDELTA unit=%llx delta=%.3fs spd=%.1f lag=%.1fm",
						(unsigned long long)validUnits[i], dsec, tgtVel.length( ), dsec * tgtVel.length( ) );
					++s_scatDeltaLog;
				}
				unit.sampleTime = unitScatterTime;   // 渲染端外推基准（esp 平滑）
			}

			// 距离：unit.distance = 2D 水平；另存 3D 直线距离用于弹道飞行时间
			unit.distance = local_position.dist_to( buffers[i].position );
			unit.distance3d = ( buffers[i].position - local_position ).length( );

			// 可见性检测：flags 0x800 位疑似"可见"标志（第三方验证逻辑）
			// 近距离(<=230m)按第三方惯例保守判定为可见，远处以标志位为准
			unit.unitFlags = buffers[i].flags;
			unit.bVisible = ( unit.distance <= 230.0f ) || ( ( buffers[i].flags & 0x800 ) != 0 );

			// 弹道预测：提前量（目标速度×飞行时间）+ 下坠补偿（0.5*g*t^2）
			// 落点基准：默认车体包围盒中部；ballisticAimPart>0 时取指定类别的部件盒中心
			// （同类部件取离本地玩家最近者——炮线最短、最易击穿），部件未加载回落车体
			if ( misc::bBallisticPrediction && computedData.ballisticVelocity > 10.0f )
			{
				vec3_t targetPos = unit.worldOrigin;
				targetPos.y += ( buffers[i].bbmin.y + buffers[i].bbmax.y ) * 0.5f;
				float fDist = ( unit.distance3d > 8.0f ) ? unit.distance3d : unit.distance;

				unit.aimPartIdx = -1;   // 默认回落车体；部件段就绪后二次修正

				const float fTime = fDist / computedData.ballisticVelocity;

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

			// 幽灵记忆列表：入列/刷新（addr 命中→更新；玩家名同人且旧条目非活跃→迁移；否则新建）
			ghost_upsert( unit, unitScatterTime );

				computedData.units.push_back( std::move( unit ) );
		}

		// --- 幽灵探测 pass：已从列表消失（lastSeen≠本周期）且 addr 不在本周期列表者，
		//     同步平读判死（仿空战外推读法：≤32 条/周期、平坦字段不追指针、读失败保留）---
		{
			int ghostProbes = 0;
			for ( size_t k = 0; k < g_ghostList.size( ); )
			{
				SGhostEntry& gh = g_ghostList[ k ];
				if ( gh.lastSeen == unitScatterTime ) { ++k; continue; }   // 活跃条目

				bool inList = false;
				for ( size_t m = 0; m < numUnits; ++m )
					if ( validUnits[ m ] == gh.addr ) { inList = true; break; }
				if ( inList ) { ++k; continue; }   // 仍在列表（迷雾态）：零成本判死覆盖

				if ( ghostProbes >= kGhostProbeMax || gh.addr < 0x10000 ) { ++k; continue; }
				++ghostProbes;

				uint16_t st = 0; uint32_t fl = 0; uint8_t ty = 0;
				const bool okRead =
					TargetProcess->Read( gh.addr + offsets::unit_offsets::unitState_offset, &st, sizeof( st ) )
					&& TargetProcess->Read( gh.addr + offsets::unit_offsets::unitFlags1_offset, &fl, sizeof( fl ) )
					&& TargetProcess->Read( gh.addr + offsets::unit_offsets::unitType_offset, &ty, sizeof( ty ) );
				if ( okRead && ghost_is_dead_feature( st, fl, ty ) )
				{
					LOG( "[GHOST] dead(probe) addr=%llX st=%u fl=0x%08X ty=%u name=%s pname=%s\n",
						(unsigned long long)gh.addr, st, fl, ty, gh.dispName.c_str( ), gh.playerName.c_str( ) );
					g_ghostList.erase( g_ghostList.begin( ) + k );
				}
				else
					++k;   // 未死或读失败 → 保留（fail-safe）
			}
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

		// --- DamageModel 乘员/弹药/油箱/炮闩部件标记：按需拉取 + 模型→世界变换 ---
		// 几何是静态资产（docs 9.25 节；第 13 轮实测 maxTdelta=0 无 [MESH-DYN] → 配位确认静态）：
		// ★成功拉取一次后永不重拉（2s 节流重拉是数据线程 ~1.1s/轮的最大开销源——每次拉取
		//   数百个部件名 DMA 读）；失败 10s 重试。
		// ★部件只在地面模式 + 坦克(type=3)上绘制：飞机模式 / 空中单位整块跳过，零开销。
		if ( !computedData.bLocalIsPlane
			&& ( bPartMarkersCrew || bPartMarkersAmmo || bPartMarkersFuel || bPartMarkersBreech ) )
		{
			// ★预算 6→2（2026-10-06）：新单位批量入场时一次拉 6 个 = 数百次串行 DMA 读
			//   → 渲染线程 960ms 级卡顿（HITCHSTAT maxgap=958~972ms 实测）。2/轮摊薄，
			//   满缓存多花几个周期（数据线程提速后一轮 ~130ms，无感）
			int fetchBudget = 2;
			const auto nowMesh = std::chrono::steady_clock::now( );

			for ( auto& u : computedData.units )
			{
				if ( !u.bValidEnemy || u.unitType != 3 )
					continue;

				SUnitMesh& mesh = g_meshCache[u.unitAddr];
				const bool needFetch = !mesh.valid
					? ( nowMesh - mesh.lastFetch ) >= std::chrono::seconds( 10 )   // 失败 10s 重试
					: ( mesh.fetchCount == 0 );                                    // 成功即终版，静态几何不重拉
				if ( needFetch && fetchBudget > 0 )
				{
					--fetchBudget;
					if ( !fetch_unit_mesh( u.unitAddr, mesh ) )
					{
						mesh.valid = false;
						mesh.lastFetch = nowMesh;	// 失败也节流，避免每周期重试
					}
				}

				if ( mesh.valid && !mesh.boxes.empty( ) )
				{
					u.partBoxes.resize( mesh.boxes.size( ) );
					for ( size_t k = 0; k < mesh.boxes.size( ); ++k )
					{
						u.partBoxes[k].cls = mesh.boxes[k].cls;
						for ( int c = 0; c < 8; ++c )
							u.partBoxes[k].corners[c] = u.worldOrigin + u.rotation.transform( mesh.boxes[k].corners[c] );
					}
				}

				// 弹道落点部位二次修正：partBoxes 本帧世界坐标已就绪，把预测重算到指定部件盒中心
				// （同类部件取离本地玩家最近者——炮线最短最易击穿；该单位无目标类别部件时回落车体中心）
				if ( misc::bBallisticPrediction && misc::ballisticAimPart > 0
					&& computedData.ballisticVelocity > 10.0f && u.bHasAimPoint )
				{
					const EPartClass want = ( misc::ballisticAimPart == 1 ) ? EPartClass::Breech :
						( misc::ballisticAimPart == 2 ) ? EPartClass::Ammo : EPartClass::Crew;
					float best = 1e30f;
					int idx = -1;
					vec3_t bestC{};
					for ( size_t k = 0; k < u.partBoxes.size( ); ++k )
					{
						if ( u.partBoxes[ k ].cls != want )
							continue;
						vec3_t c{};
						for ( const auto& cn : u.partBoxes[ k ].corners )
						{
							c.x += cn.x; c.y += cn.y; c.z += cn.z;
						}
						c.x /= 8.0f; c.y /= 8.0f; c.z /= 8.0f;
						const float d = ( c - local_position ).length( );
						if ( d < best )
						{
							best = d;
							idx = static_cast< int >( k );
							bestC = c;
						}
					}
					if ( idx >= 0 )
					{
						const float fTime = best / computedData.ballisticVelocity;
						vec3_t aim = bestC + u.velocity * fTime;
						aim.y += 0.5f * 9.81f * fTime * fTime;
						u.aimPoint = aim;
						u.aimPartIdx = idx;
					}
					else
						u.aimPartIdx = -1;
				}
			}

			// 缓存清理（30s 一次）：移除 60s 未刷新的条目（单位已销毁/离场）
			static auto s_lastMeshCleanup = std::chrono::steady_clock::now( );
			if ( ( nowMesh - s_lastMeshCleanup ) >= std::chrono::seconds( 30 ) )
			{
				s_lastMeshCleanup = nowMesh;
				for ( auto it = g_meshCache.begin( ); it != g_meshCache.end( ); )
				{
					if ( ( nowMesh - it->second.lastFetch ) >= std::chrono::seconds( 60 ) )
						it = g_meshCache.erase( it );
					else
						++it;
				}
			}
		}

		computedData.bIsValid = true;

		// --- 幽灵快照输出：本周期未刷新（lastSeen≠T）的条目 → 渲染端 15s 窗口绘制 ---
		for ( const auto& gh : g_ghostList )
		{
			if ( gh.lastSeen == unitScatterTime )
				continue;
			SGhostUnit gu;
			gu.corners = gh.corners;
			gu.velocity = gh.velocity;
			gu.dispName = gh.dispName;
			gu.lastSeen = gh.lastSeen;
			computedData.ghosts.push_back( std::move( gu ) );
		}

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

					// [BULLET] 弹丸追踪统计：数量 + 首条样本（Phase C 渲染数据验证用）
					{
						const auto& bl = g_gameData.bullets;
						if ( !bl.empty( ) )
						{
							const auto& b0 = bl.front( );
							LOG( "[BULLET] count=%d first pos=(%.0f,%.0f,%.0f) v=%.0f dist=%.0fm\n",
								static_cast<int>( bl.size( ) ),
								b0.position.x, b0.position.y, b0.position.z, b0.speed, b0.distance );
						}
					}
				}
			}
		}

	}
