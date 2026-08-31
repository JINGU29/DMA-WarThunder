// windows / stl includes.
#include <Windows.h>
#include <cstdint>
#include <intrin.h>
#include <xmmintrin.h>
#include <array>
#include <vector>
#include <algorithm>
#include <cctype>
#include <string>
#include <chrono>
#include <thread>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <shlobj.h>
#include <filesystem>
#include <streambuf>

// Log buffer - stores all log output for file dump on exit
inline std::string g_logBuffer;
inline std::mutex g_logMutex;

// 获取当前时间戳字符串 [HH:MM:SS]
inline auto get_timestamp( ) -> std::string
{
	auto now = std::chrono::system_clock::now( );
	auto t   = std::chrono::system_clock::to_time_t( now );
	std::tm tm{};
	localtime_s( &tm, &t );
	char buf[ 32 ];
	std::snprintf( buf, sizeof( buf ), "[%02d:%02d:%02d] ", tm.tm_hour, tm.tm_min, tm.tm_sec );
	return buf;
}

// 获取当前时间戳宽字符串 [HH:MM:SS]
inline auto get_timestamp_w( ) -> std::wstring
{
	auto now = std::chrono::system_clock::now( );
	auto t   = std::chrono::system_clock::to_time_t( now );
	std::tm tm{};
	localtime_s( &tm, &t );
	wchar_t buf[ 32 ];
	std::swprintf( buf, sizeof( buf )/sizeof( wchar_t ), L"[%02d:%02d:%02d] ", tm.tm_hour, tm.tm_min, tm.tm_sec );
	return buf;
}

#define LOG( fmt, ... ) \
	do { \
		auto _ts = get_timestamp( ); \
		std::printf( "%s" fmt, _ts.c_str( ), ##__VA_ARGS__ ); \
		{ \
			std::lock_guard< std::mutex > lock( g_logMutex ); \
			char _log_buf[ 1024 ]; \
			snprintf( _log_buf, sizeof( _log_buf ), "%s" fmt, _ts.c_str( ), ##__VA_ARGS__ ); \
			g_logBuffer += _log_buf; \
		} \
	} while( 0 )

#define LOGW( fmt, ... ) \
	do { \
		auto _ts = get_timestamp_w( ); \
		std::wprintf( L"%s" fmt, _ts.c_str( ), ##__VA_ARGS__ ); \
		{ \
			std::lock_guard< std::mutex > lock( g_logMutex ); \
			wchar_t _wlog_buf[ 1024 ]; \
			swprintf( _wlog_buf, sizeof( _wlog_buf )/sizeof(wchar_t), L"%s" fmt, _ts.c_str( ), ##__VA_ARGS__ ); \
			std::wstring _wstr( _wlog_buf ); \
			g_logBuffer += std::string( _wstr.begin( ), _wstr.end( ) ); \
		} \
	} while( 0 )

// 获取用于文件名的时间戳字符串 YYYY-MM-DD_HH-MM-SS
inline auto get_file_timestamp( ) -> std::string
{
	auto now = std::chrono::system_clock::now( );
	auto t   = std::chrono::system_clock::to_time_t( now );
	std::tm tm{};
	localtime_s( &tm, &t );
	char buf[ 64 ];
	std::snprintf( buf, sizeof( buf ), "%04d-%02d-%02d_%02d-%02d-%02d",
		tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
		tm.tm_hour, tm.tm_min, tm.tm_sec );
	return buf;
}

// 程序启动时确定日志文件路径，整个运行期间复用同一文件名
inline std::string g_logFilePath;

inline auto init_log_file_path( ) -> void
{
	const char* dir = "C:\\Users\\lin\\AppData\\Roaming\\war-thunder-data\\log";
	CreateDirectoryA( dir, nullptr );
	char path[ 512 ];
	snprintf( path, sizeof( path ), "%s\\log_%s.txt", dir, get_file_timestamp( ).c_str( ) );
	g_logFilePath = path;
}

inline auto dump_log_to_file( ) -> void
{
	if ( g_logFilePath.empty( ) )
		return;

	OutputDebugStringA( "[dump_log_to_file] called\n" );

	std::lock_guard< std::mutex > lock( g_logMutex );
	FILE* f = nullptr;
	// 使用二进制模式写入，避免 ccs 模式对缓冲区大小的偶数要求断言
	if ( fopen_s( &f, g_logFilePath.c_str( ), "wb" ) == 0 && f )
	{
		// 写入 UTF-8 BOM 头，确保记事本等编辑器正确识别编码
		unsigned char bom[ 3 ] = { 0xEF, 0xBB, 0xBF };
		fwrite( bom, 1, 3, f );
		fwrite( g_logBuffer.c_str( ), 1, g_logBuffer.size( ), f );
		fclose( f );
		OutputDebugStringA( "[dump_log_to_file] file written\n" );
	}
	else
	{
		OutputDebugStringA( "[dump_log_to_file] fopen_s failed\n" );
	}
}

// Crash handler - dump log on unhandled exception
inline LONG WINAPI crash_handler( EXCEPTION_POINTERS* )
{
	dump_log_to_file( );
	return EXCEPTION_EXECUTE_HANDLER;
}

// Call this at the very start of main()
inline auto init_log_system( ) -> void
{
	init_log_file_path( );
	SetUnhandledExceptionFilter( crash_handler );
	std::atexit( dump_log_to_file );
}

inline uint64_t baseAddr = 0x0;
inline uint64_t baseSize = 0x0;

// imgui includes.
#define IMGUI_DEFINE_MATH_OPERATORS
#include "..\lib\imgui\imgui.h"
#include "..\lib\imgui\backends\imgui_impl_dx11.h"
#include "..\lib\imgui\backends\imgui_impl_win32.h"
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

// other includes.
#include "memory\memory.h"

#include "game\sdk.hpp"

#include "utils\utils.hpp"

#include "features\features.hpp"

#include "core\update.hpp"

#include "core\core.hpp"

#include "gui\menu.hpp"