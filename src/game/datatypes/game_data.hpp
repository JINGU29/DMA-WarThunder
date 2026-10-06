#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include "vector3.hpp"
#include "vector2.hpp"
#include "matrix.hpp"
#include "vector3.hpp"

// 游戏上下文：cGame / cCamera / 视角矩阵
struct SImGuiGame
{
	uintptr_t cGame = 0;
	uintptr_t cCamera = 0;
	matrix4x4_t viewMatrix;
};

// AABB 包围盒
struct AABB
{
	vec3_t m_min;
	vec3_t m_max;

	AABB() : m_min( vec3_t( 0.0f, 0.0f, 0.0f ) ), m_max( vec3_t( 0.0f, 0.0f, 0.0f ) ) {}
	AABB( const vec3_t& min, const vec3_t& max ) : m_min( min ), m_max( max ) {}
};

// DamageModel 部件类别（分类器：关键词表，见 misc::classify_part）
enum class EPartClass : uint8_t
{
	None = 0,
	Crew,	// 乘员（pilot/gunner/driver/commander/loader/radio…）
	Ammo,	// 弹药（ammo rack/shell/powder/magazine…）
	Fuel,	// 油箱（fuel_tank_*_dm：主油箱/左右油箱/外挂油箱）
	Breech	// 炮闩（cannon_breech_*_dm）
};

// DamageModel 部件盒（网格束解码）：8 角点
// 在 misc::g_meshCache 中 corners=模型空间；在 SImGuiUnit::partBoxes 中 corners=世界空间（数据线程已变换）
struct SPartBox
{
	EPartClass cls = EPartClass::None;
	std::array<vec3_t, 8> corners{};
};

// 单个单位的预计算渲染数据
struct SImGuiUnit
{
	bool bValidEnemy = false;
	bool bOnScreen = false;

	uintptr_t unitAddr = 0;

	// 单位标志位（unit + 0x90 起 4 字节，m_UnitFlags1-4）
	uint32_t unitFlags = 0;
	// 单位类型（unit + 0x8C，m_UnitType），Phase 0 探测用
	uint8_t unitType = 0;
	// 单位索引（unit + 0x8，int16）——导弹制导 TargetUnitId 与此比对反查锁定目标
	int16_t unitIndex = -1;
	// 可见性（数据线程计算，渲染线程着色用）：可见=true 被遮挡=false
	bool bVisible = true;

	// 载具名字（从游戏内存读取的 UTF-8 字符串）
	std::string vehicleName;

	vec3_t worldOrigin;
	AABB worldBounds;
	matrix3x4_t rotation;

	uint8_t team = 0;
	uint16_t unitState = 0;
	uint8_t reloadTime = 0;

	float distance = 0.0f;
	// 3D 直线距离（比 distance 更贴近飞行轨迹的直线距离，用于弹道预测飞行时间）
	float distance3d = 0.0f;

	// 8 个世界坐标顶点（预计算，渲染线程做 world_to_screen）
	std::array<vec3_t, 8> worldCorners;

	// DamageModel 乘员/弹药部件盒（世界空间；Style 1 渲染：真实盒线框+远距兜底框）
	std::vector<SPartBox> partBoxes;

	// 速度（用于 aimbot 预测）
	vec3_t velocity;

	// 数据采样时刻（渲染线程外推用：renderPos = pos + vel × age，平滑高速单位跳动）
	std::chrono::steady_clock::time_point sampleTime;

	// aimbot 预计算
	bool bHasAimPoint = false;
	vec3_t aimPoint;

	// 弹道落点部位选择：被选中的部件盒在 partBoxes 中的索引（-1=未选中，回落车体中心）
	int aimPartIdx = -1;
};

// 幽灵盒（战争迷雾记忆列表的渲染快照；数据线程写、渲染线程读）
// 语义：确认过的敌人消失后 7 秒窗口内绘制——前 2 秒按消失前速度滑行、之后钉住。
// lastSeen = 最后一次活跃刷新的采样时刻（数据线程 unitScatterTime），渲染端 age=now−lastSeen。
struct SGhostUnit
{
	std::array<vec3_t, 8> corners{};   // 消失前最后 8 个世界角点
	vec3_t velocity{};                 // 消失前速度（渲染端外推：exDelta=vel×min(age,2s)）
	std::string dispName;              // 型号名（盒顶显示）
	std::chrono::steady_clock::time_point lastSeen;
};

// 单发在飞弹丸（数据线程写、渲染线程读；来源 ECS bullet_component，链路见 docs/弹丸追踪-逆向编年史.md）
// 采集端已过滤：死槽（全零/非有限值）、飞机（行寿命>30s 的恒速记录）
struct SImGuiBullet
{
	vec3_t position;       // 世界坐标（tracer +0x130）
	vec3_t velocity;       // m/s（tracer +0x124）
	float  speed = 0.0f;   // |velocity|
	float  distance = 0.0f; // 距本地玩家 3D 距离
};

// 在飞导弹/炸弹（数据线程写、渲染线程读；来源 EM 查询选择器链，offsets::missiles）
struct SImGuiMissile
{
	vec3_t position;        // 世界坐标（projectile +0x2C8）
	vec3_t velocity;        // m/s（projectile +0x2E4）
	float  speed = 0.0f;
	float  distance = 0.0f; // 距本地玩家 3D 距离
	std::string name;       // 武器名（R-77-1 / Kh-38MT / GBU…；解析失败为空）
	bool isBomb = false;    // true=炸弹（bomb 选择器） false=导弹（rocket 选择器）
	bool own = false;       // owner == 本地玩家单位
	uintptr_t projAddr = 0; // 弹体指针（跨帧稳定 source_id）

	// 制导信息（Guidance 结构；2.59 候选偏移待验）
	bool isLocked = false;      // guidance+0x50
	bool isTracking = false;    // guidance+0x51
	bool hasTarget = false;     // 目标反查成功（TargetUnitId 匹配到 units 列表中的单位）
	int targetUnitId = -1;      // guidance+0x8C 原始 s16
	bool targetIsLocal = false; // 目标 == 本地玩家单位（来袭警告）
	std::string targetName;     // 被锁定单位名（反查成功时）
	vec3_t targetPos;           // 被锁定单位位置（画弹→目标连线）
	std::chrono::steady_clock::time_point sampleTime; // 数据采样时刻（渲染外推用）
};

// 整局游戏共享数据（数据线程写、渲染线程读）
struct SGameData
{
	bool bIsValid = false;

	SImGuiGame gameCtx;

	vec3_t localPosition;
	bool bLocalIsPlane = false;

	// 弹道数据（预缓存）
	float ballisticMass = 0.0f;
	float ballisticCaliber = 0.0f;
	float ballisticVelocity = 0.0f;
	float ballisticLength = 0.0f;
	float ballisticMaxDist = 0.0f;

	// 炸弹落点
	bool bHasBombImpact = false;
	vec3_t bombImpactPoint;

	// 单位列表
	std::vector<SImGuiUnit> units;

	// 在飞弹丸（真炮弹）
	std::vector<SImGuiBullet> bullets;

	// 在飞导弹/炸弹（含名字与敌我归属）
	std::vector<SImGuiMissile> missiles;

	// 幽灵盒快照（记忆列表中本周期未刷新的条目；渲染端按 7s 窗口绘制）
	std::vector<SGhostUnit> ghosts;

	// 导弹 CCIP 落点（ballistics 容器 +0x1C9C 候选；飞机模式且读数有效时置位）
	bool bHasRocketImpact = false;
	vec3_t rocketImpactPoint;
};
