#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include "vector3.hpp"
#include "vector2.hpp"
#include "matrix.hpp"

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

// 单个单位的预计算渲染数据
struct SImGuiUnit
{
	bool bValidEnemy = false;
	bool bOnScreen = false;

	uintptr_t unitAddr = 0;

	vec3_t worldOrigin;
	AABB worldBounds;
	matrix3x4_t rotation;

	uint8_t team = 0;
	uint16_t unitState = 0;
	uint8_t reloadTime = 0;

	float distance = 0.0f;

	// 8 个世界坐标顶点（预计算，渲染线程做 world_to_screen）
	std::array<vec3_t, 8> worldCorners;

	// 速度（用于 aimbot 预测）
	vec3_t velocity;

	// aimbot 预计算
	bool bHasAimPoint = false;
	vec3_t aimPoint;
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
};
