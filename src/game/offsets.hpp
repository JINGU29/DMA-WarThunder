#pragma once

#include <cstdint>

// War Thunder 2.59.0.46 offsets - dumped from WT/OPS (monkrel.cc)
// 2026-10-05 同步 API 2.59.0.46（65 项）: globals 整体平移 +0x1F00/+0x2030/+0x2080；
// 结构体内部偏移经逐项比对不变；Projectile 名字容器 +0x38（0x6E8→0x720，0x6E0→0x718）
namespace offsets
{
	// Global offsets (from module base)
	namespace globals
	{
		// g_GameContext -> c_game pointer (was cgame_offset)
		// 使用 inline 而非 constexpr，允许 update.hpp 特征码扫描后运行时覆盖
		inline uintptr_t game_context = 0x7A252E8;
		// g_LocalPlayer -> local player entity (was localplayer_offset)
		inline uintptr_t local_player = 0x79FC8C8;
		// g_MyUnit -> local player's current unit
		inline uintptr_t my_unit = 0x7A27290;
		// g_ViewAngles
		inline uintptr_t view_angles = 0x7A29DF8;
		// g_ViewMatrix
		inline uintptr_t view_matrix = 0x7A7F378;
		// g_IsScoping
		constexpr uintptr_t is_scoping = 0x7A6E4FC;
		// g_ScreenWidth
		constexpr uintptr_t screen_width = 0x7E6B4D0;
		// g_HudInfo
		inline uintptr_t hud_info = 0x7A24910;
		// g_GameOptics
		inline uintptr_t game_optics = 0x7A27160;
		// g_AirPredictionBool
		constexpr uintptr_t air_prediction_bool = 0x7E8DCA0;
		// g_AllListData (not in 2.59.0.46 API dump either, keeping old value)
		constexpr uintptr_t all_list_data = 0x74CF748;
		// g_BombListIndexPtr
		constexpr uintptr_t bomb_list_index_ptr = 0x79FFF40;
		// g_RocketListIndexPtr
		constexpr uintptr_t rocket_list_index_ptr = 0x7A01480;
		// g_UIPosArr
		constexpr uintptr_t ui_pos_arr = 0x7A71728;
	}

	namespace cgame_offsets
	{
		constexpr uintptr_t ballistics_offset = 0x3F0;
		// camera_offset 由 update.hpp 代码区签名 sig_camera_offset 运行时重定位（允许覆盖）
		inline uintptr_t camera_offset = 0x660;
		constexpr uintptr_t current_map = 0x1F0;

		// Unit lists (group 3 - active units)
		constexpr uintptr_t unit_list_1 = 0x310;
		constexpr uintptr_t unit_list_2 = 0x328;
		// unit_list_3 / unit_count_3 由 update.hpp sig_unit_enum 运行时重定位（允许覆盖）
		inline uintptr_t unit_list_3 = 0x340;

		constexpr uintptr_t unit_count_1 = 0x320;
		constexpr uintptr_t unit_count_2 = 0x338;
		inline uintptr_t unit_count_3 = 0x350;

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
			// mass/caliber 2026-10-06 DMA 实测校订：M2A4 实弹块(与 velocity 同源簇)。
			// 用 0.87kg/0.037m 在 ballistics 容器内值猎捕，唯一命中 0x2124/0x2128；
			// 旧值 0x205C/0x2060 实测不含 0.87/0.037(属武器模板块)。
			// 注：唯一消费者 GetBallisticsInfo()/BallisticsPrediction 当前全项目无调用点
			// (aimbot 实 run() 走硬编码 850 简化预测)，故这几个值现无运行影响。
			constexpr uintptr_t mass = 0x2124;
			constexpr uintptr_t caliber = 0x2128;
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
		constexpr uintptr_t velocity_offset = 0x2100;   // ★2.59.0.46 已死：实测乱码（含 float -1.0 片段），勿用
		// ★2.59.0.46 速度链重校准（2026-10-06 Orpheus 差分实测，本地飞行 155~335m/s）：
		// unit+0x1158 / unit+0x12B8 = 两份同步运动容器拷贝，速度 vec3 @ 容器+0x104（+0x134 副本）。
		// 匀速段误差 2.4m/s，急加速段游戏侧平滑滞后 ~5%；历史环 0x40 步长 ×10 位于 0x12B8 容器+0x12B8。
		// 旧地面容器+0x5C / 空中容器+0x15E4 全部失效；地面坦克速度此字段未单独验证——帧差分兜底。
		constexpr uintptr_t vel_container_a = 0x1158;
		constexpr uintptr_t vel_container_b = 0x12B8;
		constexpr uintptr_t container_velocity = 0x104;
		constexpr uintptr_t airContainer_offset = 0xD50;  // ★2.59.0.46 已死：+0x15E4 读出 (0,0,±1.2) 垃圾
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
		// 2026-10-06 静态 dump 定向验证校准 .48 基线：旧值 0x7E64848 在本 dump 已 0 引用（过期），
		// 0x7E66768 有 1604 处合法 RIP 引用且与运行时 bullets_em_sig 重定位结果一致，故更新兜底基线。
		inline uintptr_t entity_manager = 0x7E66768;

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

	// ── 导弹/炸弹查询链（2.57.1.111 社区逆向 + 2.59.0.44 用户探针实测；与 bullet 链同源 EntityManager）──
	// 选择器 = [base + globals::rocket/bomb_list_index_ptr] & 0xFFFFFF（查询选择器，非计数/数组指针）
	// indexData = [EM + 0x5E8] + selector*0x40：
	//   +0x00 必需组件数  +0x01 可选组件数  +0x02 子列表数
	//   +0x04 子列表偏移内联(≤9 个)      +0x08 >9 时外部偏移数组指针
	//   +0x18 组件偏移矩阵（总数 >16 时为指针）：[0]=弹体列偏移 [1]=active 列偏移
	// 每子列表：listData = [EM+0x178] + listOffset*0x20（布局同 bullet chunk 条目：
	//   +0x00=storage 基址 +0x08=活条数 +0x0C=shift）
	//   projectileColumn = storage + (compOff[0]<<shift)（指针数组 8B/条）
	//   activeColumn     = storage + (compOff[1]<<shift)（u8/条）
	//   active[i]==1 → projectile = [projectileColumn + i*8]
	// Projectile 字段：+0x2C8 pos(vec3)  +0x2E4 vel(vec3)  +0x48 owner(unit)
	//   导弹 +0x720 → NameCont（+0x50 名字文本）  炸弹 +0x718 → NameCont（+0x10 名字文本）
	namespace missiles
	{
		constexpr uintptr_t em_index_table   = 0x5E8;     // [EM+0x5E8] 索引表指针
		constexpr uint32_t  selector_mask    = 0xFFFFFF;
		constexpr uintptr_t idx_required     = 0x00;
		constexpr uintptr_t idx_sublists     = 0x02;
		constexpr uintptr_t idx_inline_offs  = 0x04;      // 子列表偏移内联区（≤9 个）
		constexpr uintptr_t idx_ext_offs     = 0x08;      // >9 时外部偏移数组指针
		constexpr uintptr_t idx_comp_matrix  = 0x18;      // 组件偏移矩阵（[0]=弹体列 [1]=active 列）
		constexpr uintptr_t proj_pos         = 0x2C0;     // vec3 世界坐标 ★2.59 dump 校准（rocket@0x2C0 / bomb@0x244，missile_pass STypeParam 表）
		constexpr uintptr_t proj_vel         = 0x2DC;     // vec3 速度 ★2.59 dump 校准（=pos+0x1C；rocket@0x2DC / bomb@0x260；0x2E4 只是它的 z 分量）
		constexpr uintptr_t proj_owner       = 0x58;      // owner unit 指针 ★2.59 dump 校准（tagged：低位=flag，比较前 &~1；0x48 是 u32 对(3,0x66474) 非指针）
		constexpr uintptr_t proj_namecont_ms = 0x720;     // 导弹名字容器指针 ★2.59.0.46 API（.44 探针 0x6E8，+0x38）
		constexpr uintptr_t proj_namecont_bomb = 0x718;   // 炸弹名字容器指针 ★2.59.0.46 API（.44 探针 0x6E0，+0x38）
		constexpr uintptr_t namecont_text_ms = 0x50;      // 名字文本偏移（导弹）
		constexpr uintptr_t namecont_text_bomb = 0x10;    // 名字文本偏移（炸弹）

		// 制导结构（GuidancePtr；2.57 社区源 0x648 候选——其版本 Position@0x1D0 与我们 0x2C8 不同但
		// cleanname 0x6E8 与我们一致，0x648 处于合理区间；2.59 待验，TRACE 首次 dump 原始字节可校准）
		constexpr uintptr_t proj_guidance   = 0x648;      // → Guidance 结构指针
		constexpr uintptr_t guid_isLocked   = 0x50;       // u8 0/1
		constexpr uintptr_t guid_isTracking = 0x51;       // u8
		constexpr uintptr_t guid_target_id  = 0x8C;       // s16 目标单位 UnitIndex（对比 unit+0x8）

		// ballistics 容器内导弹 CCIP 落点（c_game+0x3F0 → +0x1C9C，候选值源自 2.57 源）
		// ★2.59 实测否定：bc_ccip_dump 显示 0x1B00~0x2000 全是配置/常量（风阻参数循环、哨兵值
		// 2147440000/1000000、id 对），无任何世界坐标——落点向量应在弹体对象内部（待 0x2000 dump 校准）
		constexpr uintptr_t rocket_impact_point = 0x1C9C;

		constexpr uint32_t  max_missiles    = 16;
		constexpr uintptr_t max_shift       = 12;
	}
}
