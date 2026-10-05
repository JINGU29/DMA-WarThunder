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

	// aimbot 预计算
	bool bHasAimPoint = false;
	vec3_t aimPoint;
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
};
