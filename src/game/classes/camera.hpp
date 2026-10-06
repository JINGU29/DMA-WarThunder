#pragma once

#include <chrono>

class c_camera {
public:
	auto init( uintptr_t cGame ) -> bool {
		this->base_address = TargetProcess->Read< uintptr_t >( cGame + offsets::cgame_offsets::camera_offset );
		return this->base_address != 0;
	}

public:

	auto getCameraMatrix( ) -> matrix4x4_t {

		// ★第 4 轮打点（2026-10-05）：帧节奏探针。MATGAP = 两次矩阵读取间隔 >20ms（正常 7~14ms）；
		//   间隔即「矩阵年龄」上限，转动时视觉脱框 ≈ 角速度 × 间隔。MATFAIL = 读取失败（该帧不画框）
		static std::chrono::steady_clock::time_point s_lastReadTp{};
		static int s_gapLog = 0;
		static int s_failLog = 0;
		const auto tRead = std::chrono::steady_clock::now( );

		VMMDLL_SCATTER_HANDLE hScatter = TargetProcess->CreateScatterHandle( );
		if ( !hScatter )
			return matrix4x4_t( );

		matrix4x4_t matrix;
		if (!TargetProcess->AddScatterReadRequest( hScatter, uintptr_t( this->base_address + offsets::cgame_offsets::camera_offsets::camera_matrix_offset ), &matrix, sizeof( matrix4x4_t ) ) || !TargetProcess->ExecuteReadScatter( hScatter, 0, true ) ) {
			TargetProcess->CloseScatterHandle( hScatter );
			if ( s_failLog < 120 ) { TRACE( "MATFAIL" ); ++s_failLog; }
			return matrix4x4_t( );
		}

		if ( s_lastReadTp.time_since_epoch( ).count( ) != 0 && s_gapLog < 120 )
		{
			const float gapMs = std::chrono::duration<float, std::milli>( tRead - s_lastReadTp ).count( );
			if ( gapMs > 20.0f && gapMs < 300.0f ) { TRACE( "MATGAP gap=%.0fms", gapMs ); ++s_gapLog; }
		}
		s_lastReadTp = tRead;

		// ★ 运行时结构 sanity（2026-10-06）：版本热更后 camera_matrix_offset(0x1D8) 若漂移，
		//   会读到非矩阵垃圾 → ESP 全体框静默失效。读回后校验：非全零（未就绪）前提下，
		//   16 个 float 必须全部有限、量级合理（<1e6）、旋转区（前 12）非全零；非法则节流告警指名嫌疑偏移。
		static int s_sanityLog = 0;
		const float* fl = &matrix.m_matrix[ 0 ][ 0 ];
		bool all_zero = true, finite = true; float maxabs = 0.f; int rot_nz = 0;
		for ( int i = 0; i < 16; ++i )
		{
			const float v = fl[ i ];
			if ( v != 0.f ) all_zero = false;
			if ( !( v >= -1e30f && v <= 1e30f ) ) finite = false;   // NaN/Inf/超界
			const float a = v < 0 ? -v : v;
			if ( a > maxabs ) maxabs = a;
			if ( i < 12 && a > 1e-3f ) ++rot_nz;
		}
		if ( !all_zero && ( !finite || maxabs > 1e6f || rot_nz < 3 ) && s_sanityLog < 40 )
		{
			TRACE( "MATSANITY FAIL finite=%d maxabs=%.1f rot_nz=%d - camera_matrix_offset 0x%X may have drifted",
			       (int)finite, maxabs, rot_nz, (unsigned)offsets::cgame_offsets::camera_offsets::camera_matrix_offset );
			++s_sanityLog;
		}

		return matrix;
	}

private:
	uintptr_t base_address;
};
