#pragma once

#include <cstdint>

// War Thunder 2.59.0.44 offsets - dumped from WT/OPS (monkrel.cc)
namespace offsets
{
	// Global offsets (from module base)
	namespace globals
	{
		// g_GameContext -> c_game pointer (was cgame_offset)
		// 使用 inline 而非 constexpr，允许 update.hpp 特征码扫描后运行时覆盖
		inline uintptr_t game_context = 0x7A23268;
		// g_LocalPlayer -> local player entity (was localplayer_offset)
		inline uintptr_t local_player = 0x79FA898;
		// g_MyUnit -> local player's current unit
		constexpr uintptr_t my_unit = 0x7A25210;
		// g_ViewAngles
		constexpr uintptr_t view_angles = 0x7A27D78;
		// g_ViewMatrix
		constexpr uintptr_t view_matrix = 0x7A7D2F8;
		// g_IsScoping
		constexpr uintptr_t is_scoping = 0x7A6C47C;
		// g_ScreenWidth
		constexpr uintptr_t screen_width = 0x7E695D0;
		// g_HudInfo
		constexpr uintptr_t hud_info = 0x7A22890;
		// g_GameOptics
		constexpr uintptr_t game_optics = 0x7A250E0;
		// g_AirPredictionBool
		constexpr uintptr_t air_prediction_bool = 0x7E8BDA0;
		// g_AllListData (not in 2.59.0.44 API dump, keeping old value)
		constexpr uintptr_t all_list_data = 0x74CF748;
		// g_BombListIndexPtr
		constexpr uintptr_t bomb_list_index_ptr = 0x79FDF10;
		// g_RocketListIndexPtr
		constexpr uintptr_t rocket_list_index_ptr = 0x79FF450;
		// g_UIPosArr
		constexpr uintptr_t ui_pos_arr = 0x7A6F6A8;
	}

	namespace cgame_offsets
	{
		constexpr uintptr_t ballistics_offset = 0x3F0;
		constexpr uintptr_t camera_offset = 0x660;
		constexpr uintptr_t current_map = 0x1F0;

		// Unit lists (group 3 - active units)
		constexpr uintptr_t unit_list_1 = 0x310;
		constexpr uintptr_t unit_list_2 = 0x328;
		constexpr uintptr_t unit_list_3 = 0x340;

		constexpr uintptr_t unit_count_1 = 0x320;
		constexpr uintptr_t unit_count_2 = 0x338;
		constexpr uintptr_t unit_count_3 = 0x350;

		namespace camera_offsets
		{
			constexpr uintptr_t camera_matrix_offset = 0x1D8;
			constexpr uintptr_t camera_position_offset = 0x60;
		}

		namespace ballistic_offsets
		{
			constexpr uintptr_t bomb_impact_point = 0x1CCC;
			// Bullet impact point - not in new dump, keeping old value
			constexpr uintptr_t bullet_impact_point = 0x22C8 + 0x20;
			// Ballistics data (velocity, mass, caliber, length, max_dist)
			// velocity 用用户提供的第三方验证偏移 0x2118
			// (ballistics 容器: c_game + 0x3f0 -> + 0x2118 = 弹速 float)
			constexpr uintptr_t velocity = 0x2118;
			constexpr uintptr_t mass = 0x205C;
			constexpr uintptr_t caliber = 0x2060;
			constexpr uintptr_t length = 0x2048;
			constexpr uintptr_t max_dist = 0x2068;
			constexpr uintptr_t selected_unit_ptr = 0x6B0;
			constexpr uintptr_t ballistics_ptr = 0x3f0;
			constexpr uintptr_t ingame_ballistics = 0x2460;
			constexpr uintptr_t weapon_position = 0x1F00;

			constexpr uintptr_t telecontrol_offset = 0xcb8;
			namespace telecontrol_offsets
			{
				constexpr uintptr_t gameui_offset = 0x928;
				namespace gameui_offsets
				{
					constexpr uintptr_t mouse_pos = 0x810;
				}
			}
		}
	}

	namespace localplayer
	{
		constexpr uintptr_t guiState_offset = 0x640;
		constexpr uintptr_t controlledUnit_offset = 0x878;
		constexpr uintptr_t ownedUnit_offset = 0x848;
		constexpr uintptr_t spectatedModelIndex_offset = 0x670;
		constexpr uintptr_t name_offset = 0x60;
		// Flags for isLocal check: (player+0x90 >> 9) & 1
		constexpr uintptr_t publicFlags_offset = 0x90;
	}

	namespace unit_offsets
	{
		constexpr uintptr_t ground_velocity_offset = 0x5C;
		constexpr uintptr_t airmovement_offset = 0x10;
		constexpr uintptr_t position_offset = 0xD40;
		constexpr uintptr_t rotation_matrix_offset = 0xD1C;
		constexpr uintptr_t bbmax_offset = 0x274;
		constexpr uintptr_t bbmin_offset = 0x268;
		constexpr uintptr_t body_bbmax_offset = 0x218C;
		constexpr uintptr_t body_bbmin_offset = 0x2180;
		constexpr uintptr_t unitState_offset = 0xF98;
		constexpr uintptr_t teamNum_offset = 0x1018;
		constexpr uintptr_t unitType_offset = 0x8C;
		constexpr uintptr_t unitIndex_offset = 0x8;
		constexpr uintptr_t unitFlags1_offset = 0x90;
		constexpr uintptr_t unitFlags2_offset = 0x91;
		constexpr uintptr_t unitFlags3_offset = 0x92;
		constexpr uintptr_t unitFlags4_offset = 0x93;
		constexpr uintptr_t visualReload_offset = 0xAF0;
		constexpr uintptr_t info_offset = 0x1028;
		constexpr uintptr_t invulnerable_offset = 0xE98;
		constexpr uintptr_t invulTimer_offset = 0xE74;
		constexpr uintptr_t velocity_offset = 0x2100;
		constexpr uintptr_t airContainer_offset = 0xD50;
		constexpr uintptr_t armory_offset = 0x10D0;
		constexpr uintptr_t damageModelCont_offset = 0x10A8;
		constexpr uintptr_t playerInfo_offset = 0xFA0;
		// 地面运动容器指针（unit + 0x2100 是指针！速度在容器内 +0x5C，第三方 2.59.0.44 验证）
		constexpr uintptr_t groundmovement_offset = 0x2100;
	}

	namespace unit_info_offsets
	{
		constexpr uintptr_t bomberView_offset = 0x454;
		constexpr uintptr_t haveCCIPForBombs_offset = 0x459;
		constexpr uintptr_t haveCCIPForGun_offset = 0x457;
		constexpr uintptr_t haveCCIPForRocket_offset = 0x456;
		constexpr uintptr_t haveCCIPForTurret_offset = 0x458;
	}

	// ── DamageModel 网格束链（2026-10-04 实测；详见 docs/伤害模型模块渲染-进展与参考.md 9.25 节）──
	// 旧 grump 方案A（容器+0x78=data_list）/方案B（TB0 0x40 步长）均已失效，当前版本用下述网格束链。
	// 链路: unit+0x10A8(damageModelCont_offset) → dnet → dnet+0x58 → obj → obj+0x78 → header
	// header 块: +0x04=rel(部件表相对偏移)  +0x08=loop(部件数)  +0x4C=namePool(名字池相对偏移)  +0x50=nameSize
	// ptab  = header + rel            0x40/条: +0x08 boxMin  +0x14 boxMax(局部AABB)  +0x26 u16 HP(0xFFFF=未受损)  +0x3C u32 nameOff
	// place = ptab + loop*0x40        0x30/条 = 12 float（R3x3 行主序 f0..f8 + t f9..f11）；模型坐标 = R·局部 + t
	// 名字  = cstr(header + namePool + nameOff)
	namespace damage_model
	{
		namespace mesh_bundle
		{
			constexpr uintptr_t backref_off      = 0x38;  // u64[dnet+0x38]==unitAddr 反指校验（本地场景恒成立；联机失效改走魔数）
			constexpr uintptr_t dnet_tag_off     = 0x20;  // u32 'DNET' 魔数（联机时反指失效的替代校验，见 9.26）
			constexpr uint32_t  dnet_magic       = 0x54454E44;  // 'DNET'（小端）
			constexpr uintptr_t obj_off          = 0x58;  // dnet → obj
			constexpr uintptr_t header_off       = 0x78;  // obj → header
			constexpr uintptr_t hdr_rel_off      = 0x04;  // u32 部件表相对偏移
			constexpr uintptr_t hdr_loop_off     = 0x08;  // u32 部件数
			constexpr uintptr_t hdr_namepool_off = 0x4C;  // u32 名字池相对偏移
			constexpr uintptr_t hdr_namesize_off = 0x50;  // u32 名字池大小
			constexpr uintptr_t hdr_block_size   = 0x60;  // header 一次读取块大小
			constexpr uintptr_t part_stride      = 0x40;
			constexpr uintptr_t part_min_off     = 0x08;  // vec3 局部 boxMin
			constexpr uintptr_t part_max_off     = 0x14;  // vec3 局部 boxMax
			constexpr uintptr_t part_hp_off      = 0x26;  // u16 HP（0xFFFF=未受损）
			constexpr uintptr_t part_name_off    = 0x3C;  // u32 nameOff
			constexpr uintptr_t place_stride     = 0x30;  // 12 float: R 行主序 f0..f8 + t f9..f11
			constexpr uint32_t  max_parts        = 2048;  // loop 上限保护
			constexpr uint32_t  max_name_size    = 0x100000;
		}
	}

	// ── 弹丸追踪（live bullets，2026-10-05 实测定稿；公式与证据链详见 docs/弹丸追踪-逆向编年史.md）──
	// ECS EntityManager（.bss 全局对象本体，非指针）→ 哈希表查 bullet_component(typeId) → 掩码定位
	// bullet archetype → chunk 描述符取记录数组基址 → 0x3C0 步长记录：
	//   record = chunk_base + (compOff << shift) + row * 0x3C0
	//   +0x124 = vec3 速度(m/s)   +0x130 = vec3 位置(米)
	// 实测：干净轨迹 |Δpos/dt| vs |vel| 误差 1.6%~4%；长寿命恒速记录是飞机、全零位是死槽（采集端过滤）。
	namespace bullets
	{
		// EM 在模块内的 RVA（inline 允许 update.hpp 签名扫描运行时覆盖）
		inline uintptr_t entity_manager = 0x7E64848;

		constexpr uint32_t  comp_hash = 0xBC84D211;  // bullet_component 描述哈希（EM+0x260 哈希表键）
		constexpr uint32_t  type_hash = 0xD5EFE099;  // 类型校验哈希（EM+0x278 表项高 32 位 = getter 第 4 参）

		constexpr uintptr_t record_size     = 0x3C0;  // 单条弹丸记录大小（getter 第 5 参实测）
		constexpr uintptr_t tracer_velocity = 0x124;  // vec3 速度 m/s
		constexpr uintptr_t tracer_position = 0x130;  // vec3 位置（米）

		// EM 内部字段（2.59.0.44 getter 全解实测；版本更新失效时按编年史 2.1 重导）
		constexpr uintptr_t em_chunk_desc   = 0x178;  // chunk 描述符数组指针（0x20/条按 archetypeIdx 索引）
		constexpr uintptr_t em_prefix_table = 0x180;  // 前缀表指针（u32/条按 archetypeIdx）
		constexpr uintptr_t em_arch_meta    = 0x188;  // archetype 元数据表指针（0x10/条：+0=掩码表指针,+8=compStart,+A=compEnd）
		constexpr uintptr_t em_offset_table = 0x210;  // 组件偏移表指针（u16/条按全局 slot 序号）
		constexpr uintptr_t em_hash_table   = 0x260;  // 组件哈希表指针（0x0C/条：flag@0, hash@4, typeId@8）
		constexpr uintptr_t em_hash_mask    = 0x268;  // 哈希掩码 u32（桶 = mask & comp_hash，线性探测步长 0xC）

		constexpr uint32_t  max_shift       = 12;     // shift=log2(容量) 上限保护（实测 64 槽 chunk → 6）
		constexpr uint32_t  max_bullets     = 256;    // 单帧弹丸读取上限（实测真炮弹并发 <100）
	}
}
