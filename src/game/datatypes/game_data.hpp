#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include "vector3.hpp"
#include "vector2.hpp"
#include "matrix.hpp"

// AABB 包围盒
struct AABB
{
	vec3_t m_min;
	vec3_t m_max;

	AABB( ) : m_min( vec3_t( 0.0f, 0.0f, 0.0f ) ), m_max( vec3_t( 0.0f, 0.0f, 0.0f ) ) {}
	AABB( vec3_t min, vec3_t max ) : m_min( min ), m_max( max ) {}
};

// 屏幕顶点
struct SImGuiVert
{
	bool bOnScreen{ false };
	vec2_t origin;
};

// 游戏上下文（相机等）
struct SImGuiGame
{
	uintptr_t cGame{ 0 };
	uintptr_t cCamera{ 0 };
	matrix4x4_t viewMatrix;
};

// 预计算的单元渲染数据
struct SImGuiUnit
{
	bool bOnScreen{ false };
	bool bValidEnemy{ false };
	uintptr_t unitAddr{ 0 };

	vec3_t worldOrigin;
	AABB worldBounds;
	matrix3x4_t rotation;
	vec2_t screenOrigin;
	std::array<SImGuiVert, 8> screenBoxVerts;

	float distance{ 0.0f };
	uint8_t team{ 0 };
	uint16_t unitState{ 0 };

	// aimbot 预计算字段
	vec3_t velocity;
	vec3_t aimPoint;
	bool bHasAimPoint{ false };
};

// 完整的共享游戏数据
struct SGameData
{
	SImGuiGame gameCtx;
	std::vector<SImGuiUnit> units;
	vec3_t localPosition;
	bool bIsValid{ false };
	bool bLocalIsPlane{ false };

	// 弹道数据（aimbot 用）
	float ballisticMass{ 0.0f };
	float ballisticCaliber{ 0.0f };
	float ballisticVelocity{ 0.0f };
	float ballisticLength{ 0.0f };
	float ballisticMaxDist{ 0.0f };

	// 炸弹落点（飞机模式）
	vec3_t bombImpactPoint;
	bool bHasBombImpact{ false };
};
