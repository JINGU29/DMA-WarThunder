#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

// based on https://github.com/bditt/WarThunder-Offset-Dumper
// 扩展：加入 9.17 伤害模型特征码，实现版本更新后自动重定位 DM 入口链等偏移
//
// 工作流程:
//   1. 读取 aces.exe 内版本字符串
//   2. 若 offsets 文件存在且版本一致 → 直接加载缓存偏移
//   3. 若版本变化或文件不存在 → 执行特征码扫描，写入新的 offsets 文件
//
// 特征码列表（9.17 扫描验证，全部唯一命中）:
//   sig_c_game        → c_game 全局指针偏移
//   sig_c_local       → c_local 全局指针偏移
//   sig_dm_entry      → DM 入口链: DM = *(unit+0x10A8) + 0x370
//   sig_dm_get_hp     → dm_get_part_hp 函数（定位 HP 读取）
//   sig_dm_get_props  → dm_get_damage_part_props 函数（partId→+0x88 记录访问器）

namespace update
{
    // ── 异步校准状态（★2026-10-06：扫描移后台线程，渲染层读这些原子量显示进度，
    //    替代"启动即白屏无响应"——扫描期间 overlay 显示进度条 + 菜单照常响应）──
    inline std::atomic<int>  g_scanStage    { 0 };   // 0=等待 1=窗口 2=全映像 3=重试 4=收尾 5=就绪
    inline std::atomic<int>  g_scanPermille { 0 };   // 全映像阶段进度 ‰
    inline std::atomic<bool> g_ready        { false };// 偏移就绪（SDK 初始化完成，业务线程放行）
    inline std::atomic<bool> g_calibFailed  { false };

    // ── 特征码定义 ──────────────────────────────────────────────
    // c_game: mov rax, [rip+disp32] → 48 8B 05 ? ? ? ? F2 0F 10 4F
    constexpr const char* sig_c_game      = "48 8B 05 ? ? ? ? F2 0F 10 4F";
    // c_local: cmp [rip+disp32], rcx → 48 39 0D ? ? ? ? 75 ? 48 C7 05
    //   （2.59.0.48 复测未失效：唯一命中 aces.exe+0x596182，解析 = g_LocalPlayer 0x79FC8C8；
    //    22:03 首扫失败为整读瞬态，非模式变化 —— 见下方扫描策略加固）
    constexpr const char* sig_c_local     = "48 39 0D ? ? ? ? 75 ? 48 C7 05";
    // ── 其余全局根（2026-10-05 dm_grootsig.py 对 2.59.0.48 生成，均全映像唯一命中）──
    // 统一 disp32@+3 / instr_len=7（与 resolve_global_offset 默认一致）；尾部 rip/rel32 位移已遮 ??
    constexpr const char* sig_view_matrix = "0F 11 15 ? ? ? ? 41 0F 10 55 00 41 0F 10 5D";   // @+0x1a6d7d2 (movups [g_ViewMatrix],xmm)
    constexpr const char* sig_hud_info    = "48 8B 05 ? ? ? ? 0F B6 40 35 83 F0 01 80 BC";    // @+0x236fe9
    constexpr const char* sig_game_optics = "48 8B 05 ? ? ? ? 80 B8 FE 2A 00 00 01 0F 85";    // @+0x2390da
    constexpr const char* sig_my_unit     = "48 8B 05 ? ? ? ? 31 F6 48 85 C0 74 17 8B 88";    // @+0x22ed6f
    constexpr const char* sig_view_angles = "0F 11 1D ? ? ? ? F3 0F 11 05 ? ? ? ? 48 8B 05 D8 84"; // @+0x24cdfa
    // DM 入口链: mov rcx,[rsi+0x10A8]; mov eax,0x370; add rcx,rax → 48 8B 8E A8 10 00 00 B8 70 03 00 00 48 01 C1
    // 命中点直接编码了 unit+0x10A8 和 +0x370，用于验证偏移有效性
    constexpr const char* sig_dm_entry    = "48 8B 8E A8 10 00 00 B8 70 03 00 00 48 01 C1";
    // dm_get_part_hp: 0F BF 02 0F 57 C0 39 41 50 76 4A 4C 8B 81 88 00 00 00 89 C2 4C 8D 0C 92
    constexpr const char* sig_dm_get_hp   = "0F BF 02 0F 57 C0 39 41 50 76 4A 4C 8B 81 88 00 00 00 89 C2 4C 8D 0C 92";
    // dm_get_damage_part_props: 48 0F BF 02 89 C0 48 8D 04 80 48 C1 E0 02 48 03 81 88 00 00 00 C3 CC CC
    constexpr const char* sig_dm_get_props= "48 0F BF 02 89 C0 48 8D 04 80 48 C1 E0 02 48 03 81 88 00 00 00 C3 CC CC";
    // 弹丸追踪 EM 全局（2026-10-05 编年史 §2.4）：bullet spawn 处 mov dword[rsp+X],0x3C0（记录大小）
    //   + lea rcx,[EM] + lea r8,&desc + mov edx,entityId + mov r9d,0xD5EFE099（bullet typeHash）+ call getter(0x2c06b0)
    // 本版 2 处命中且全部解析到同一 EM RVA（0x7E64848），解析式：EM = hit + 15 + disp32（disp32 @ hit+11）
    // ⚠尾部必须带空格：FindSignature 对末尾 '?' token 的 pat[2] 越界判断依赖 "? \0" 排布（坑位 9）
    constexpr const char* sig_bullets_em  = "C7 44 24 ? C0 03 00 00 48 8D 0D ? ? ? ? 4C 8D 44 24 ? 89 ? 41 B9 99 E0 EF D5 E8 ? ? ? ? ";
    // ── ESP 关键结构链（2026-10-06 dm_structcheck.py 对 2.59.0.48 实测，均全映像唯一命中 ×1）──
    // camera_offset: mov rax,[rip+game_ctx]; test rax,rax; je; mov rcx,[rax+0x660]; test rcx; je; mov edx,ebx
    //   camera_offset = u32 disp @ hit+15（@ aces.exe+0x2ade1a）。遮掉 game_ctx disp32/je rel8/camera disp32
    constexpr const char* sig_camera_offset = "48 8B 05 ? ? ? ? 48 85 C0 74 ? 48 8B 88 ? ? ? ? 48 85 C9 74 ? 89 DA";
    // unit 枚举链: mov rax,[rip+game_ctx]; mov rcx,[rax+?]; mov rdx,[rax+unit_list_3]; mov eax,[rax+unit_count_3]; mov [rsp+..],rdx
    //   unit_list_3 = u32 disp @ hit+17，unit_count_3 = u32 disp @ hit+23（@ aces.exe+0xe0488d）
    constexpr const char* sig_unit_enum     = "48 8B 05 ? ? ? ? 48 8B 88 ? ? ? ? 48 8B 90 ? ? ? ? 8B 80 ? ? ? ? 48 89 94 24";

    // 版本字符串在 aces.exe 中的偏移（2.59.0.44 实测）
    // 注意：游戏会用自定义打包器把模块尾部数据区（0x6000000+）动态映射/卸载，此 RVA 可能落进已卸载的
    // 洞里（2026-10-05 实测 0x6631350 整页不可读）——读不到时用 scan_version_string 窗口扫描兜底
    constexpr uintptr_t version_string_rva = 0x6631350;

    // ── 解析 RIP 相对寻址 ──────────────────────────────────────
    // 指令格式: 48 8B 05 [disp32]  → 长度 7, 目标 = 指令地址 + 7 + disp32
    // 指令格式: 48 39 0D [disp32]  → 长度 7, 目标 = 指令地址 + 7 + disp32
    inline auto resolve_rip_relative( uint64_t instr_addr, int32_t disp, uint8_t instr_len = 7 ) -> uint64_t
    {
        return instr_addr + instr_len + disp;
    }

    // ── 从特征码命中地址解析全局指针偏移 ──────────────────────
    // c_game 特征码: 48 8B 05 [disp32@+3] → 读 disp, 目标 = sig_addr + 7 + disp → RVA = 目标 - baseAddr
    inline auto resolve_global_offset( uint64_t sig_addr, int32_t disp_offset = 3, uint8_t instr_len = 7 ) -> uintptr_t
    {
        int32_t disp = TargetProcess->Read<int32_t>( sig_addr + disp_offset );
        uint64_t target = resolve_rip_relative( sig_addr, disp, instr_len );
        return target - baseAddr;
    }

    // ── 布局指纹：对已存档签名命中点的原字节做 FNV-1a ──────────
    // 游戏热更后这些位置的指令字节必然变化 → 指纹失配 → 自动重扫；
    // 字节一致 → 偏移依然有效 → 直接加载缓存。比读版本串可靠（本版映像内无 ASCII 版本串）。
    inline auto compute_fingerprint( const std::unordered_map<std::string, uintptr_t>& m ) -> uint64_t
    {
        uint64_t h = 0xcbf29ce484222325ULL;
        auto mix = [ &h ]( const uint8_t* p, size_t n )
        {
            for ( size_t i = 0; i < n; ++i )
            {
                h ^= p[ i ];
                h *= 0x100000001b3ULL;
            }
        };
        // key 在文件里存的是 RVA；读不到/键缺失都参与混 hash（确定性）
        auto site = [ & ]( const char* key, size_t n )
        {
            auto it = m.find( key );
            if ( it == m.end( ) )
            {
                h ^= 0xFF; h *= 0x100000001b3ULL;
                return;
            }
            uint8_t buf[ 64 ] = { 0 };
            if ( it->second > 0x10000 && it->second < baseSize
                && TargetProcess->Read( baseAddr + it->second, buf, n ) )
                mix( buf, n );
            else
            {
                h ^= 0xEE; h *= 0x100000001b3ULL;
            }
        };
        site( "dm_entry", 14 );        // 48 8B 8E A8 10 00 00 B8 70 03 00 00 48 01 C1
        site( "dm_get_hp", 26 );
        site( "dm_get_props", 38 );
        site( "bullets_em_sig", 33 );  // 弹丸 EM 签名命中点（代码区，稳定）；注意不能用 EM 对象地址（.bss 数据每帧变）
        site( "cam_off_sig", 26 );     // camera_offset 签名命中点（代码区，结构偏移漂移则此处指令字节变）
        site( "unit_enum_sig", 31 );   // unit_list_3/count_3 签名命中点
        return h;
    }

    // ── 写入 offsets 文件 ──────────────────────────────────────
    inline auto write_offsets_file( uint64_t fingerprint, uintptr_t c_game_rva, uintptr_t c_local_rva,
                                     uint64_t dm_entry_rva, uint64_t dm_get_hp_rva, uint64_t dm_get_props_rva,
                                     uintptr_t bullets_em_rva, uintptr_t bullets_em_sig_rva,
                                     const std::vector< std::pair< std::string, uintptr_t > >& extra_globals = { },
                                     const std::vector< std::pair< std::string, uintptr_t > >& struct_fields = { } ) -> void
    {
        std::ofstream outFile( "offsets", std::ios::trunc );
        if ( !outFile.is_open( ) )
            return;

        outFile << "fingerprint: 0x" << std::hex << fingerprint << std::dec << std::endl;

        // 全局指针偏移（RVA）
        if ( c_game_rva < 0x100000 )
            outFile << "get c_game failed!" << std::endl;
        else
            outFile << "c_game: 0x" << std::hex << c_game_rva << std::dec << std::endl;

        if ( c_local_rva < 0x100000 )
            outFile << "get c_local failed!" << std::endl;
        else
            outFile << "c_local: 0x" << std::hex << c_local_rva << std::dec << std::endl;

        // DM / 弹丸签名命中点（RVA，供指纹校验与日志）
        outFile << "dm_entry: 0x" << std::hex << dm_entry_rva << std::dec << std::endl;
        outFile << "dm_get_hp: 0x" << std::hex << dm_get_hp_rva << std::dec << std::endl;
        outFile << "dm_get_props: 0x" << std::hex << dm_get_props_rva << std::dec << std::endl;

        // 弹丸追踪：EM 对象 RVA（业务用）+ 签名命中点 RVA（指纹用，代码区稳定）
        outFile << "bullets_em: 0x" << std::hex << bullets_em_rva << std::dec << std::endl;
        outFile << "bullets_em_sig: 0x" << std::hex << bullets_em_sig_rva << std::dec << std::endl;

        // 其余自动重定位的全局根（仅写解析成功的）
        for ( const auto& kv : extra_globals )
            if ( kv.second >= 0x100000 )
                outFile << kv.first << ": 0x" << std::hex << kv.second << std::dec << std::endl;

        // ESP 关键结构字段偏移（小值 <0x2000，走独立低阈值通道无条件写；
        // 缺键=该轮签名未命中，加载侧保留 offsets.hpp 常量兜底，不用 operator[]）
        for ( const auto& kv : struct_fields )
            if ( kv.second > 0 && kv.second < 0x2000 )
                outFile << kv.first << ": 0x" << std::hex << kv.second << std::dec << std::endl;

        outFile.close( );
    }

    // ── 解析 offsets 文件 ──────────────────────────────────────
    inline auto parse_offsets( const std::string& filename = "offsets" ) -> std::unordered_map<std::string, uintptr_t>
    {
        std::ifstream file( filename );
        if ( !file.is_open( ) )
            return { };

        std::string line;
        std::unordered_map<std::string, uintptr_t> offset_map;

        while ( std::getline( file, line ) )
        {
            if ( line.find( "failed" ) != std::string::npos )
            {
                std::cerr << "Warning: " << line << std::endl;
                continue;
            }

            size_t colon_pos = line.find( ':' );
            if ( colon_pos == std::string::npos )
                continue;

            std::string offset_name = line.substr( 0, colon_pos );

            size_t hex_pos = line.find( "0x", colon_pos );
            if ( hex_pos == std::string::npos )
                continue;

            std::string hex_value = line.substr( hex_pos + 2 );
            std::stringstream ss;
            ss << std::hex << hex_value;

            uintptr_t value;
            if ( ss >> value )
                offset_map[ offset_name ] = value;
        }

        file.close( );
        return offset_map;
    }

    // ── 签名扫描策略（2026-10-05 加固）─────────────────────────
    // 背景：全部签名命中点都位于映像前 32MB（最远 dm_entry ~29.6MB）；游戏打包器会动态
    //       卸载尾部数据区，整读 138MB 映像的单次调用一旦撞上瞬态不可读区会整体失败
    //       （曾致 c_local 被误判"签名失效"，实为读取瞬态）。
    // 策略：先扫 [0,scan_window_rva) 稳定窗口（快约 4 倍、失败面大降），未命中回退全映像；
    //       仍失败的签名最多再延迟重试 scan_retry_rounds 轮。
    constexpr uintptr_t scan_window_rva     = 0x2000000;   // 32MB 稳定代码窗口
    constexpr int       scan_retry_rounds   = 4;   // ★2→4（2026-10-06）：DMA 整读瞬态曾连续 2 轮不退
    constexpr int       scan_retry_delay_ms = 15000;

    // 单签名扫描：窗口优先，未命中回退全映像
    inline auto scan_sig( const char* sig, const char* label ) -> uint64_t
    {
        const uint64_t win_end  = baseAddr + scan_window_rva;
        const uint64_t full_end = baseAddr + baseSize;
        auto hit = TargetProcess->FindSignature( sig, baseAddr, full_end > win_end ? win_end : full_end );
        if ( hit < 0x100000 && full_end > win_end )
        {
            LOG( "[update]   %s: window miss — full-image fallback\n", label );
            hit = TargetProcess->FindSignature( sig, win_end, full_end );
        }
        return hit;
    }

    // ── 执行特征码扫描并写入 offsets 文件 ──────────────────────
    // 返回 true = 关键签名齐全（c_game/c_local）且 offsets 文件已写入；
    // false = 页面密文期扫描失败（已保护旧文件，业务用内置常量继续）
    inline auto do_scan( ) -> bool
    {
        LOG( "[update] Starting signature scan (stable window first, full-image fallback)...\n" );

        struct SigDef { const char* sig; const char* label; };
        const SigDef defs[ 13 ] = {
            { sig_c_game, "c_game" },       { sig_c_local, "c_local" },
            { sig_dm_entry, "dm_entry" },   { sig_dm_get_hp, "dm_get_hp" },
            { sig_dm_get_props, "dm_get_props" }, { sig_bullets_em, "bullets_em" },
            { sig_view_matrix, "view_matrix" },   { sig_hud_info, "hud_info" },
            { sig_game_optics, "game_optics" },   { sig_my_unit, "my_unit" },
            { sig_view_angles, "view_angles" },   { sig_camera_offset, "camera_offset" },
            { sig_unit_enum, "unit_enum" },
        };
        std::vector<uint64_t> hits( 13, 0 );

        // ── 第一轮：32MB 稳定窗口一次多签名单遍（★13×32MB → 32MB×1）──
        g_scanStage = 1;
        {
            std::vector<const char*> pats;
            for ( const auto& d : defs ) pats.push_back( d.sig );
            std::vector<uint64_t> h;
            TargetProcess->FindSignaturesMulti( pats, baseAddr, baseAddr + scan_window_rva, h );
            for ( size_t i = 0; i < h.size( ) && i < 13; ++i )
                hits[ i ] = h[ i ];
            for ( int i = 0; i < 13; ++i )
                if ( hits[ i ] < 0x100000 )
                    LOG( "[update]   %s: window miss — full-image fallback\n", defs[ i ].label );
        }

        // ── 第二轮：未命中签名走全映像多签名单遍（★13×138MB → 138MB×1，进度回报 overlay）──
        {
            std::vector<const char*> pats;
            std::vector<size_t> map;
            for ( size_t i = 0; i < 13; ++i )
                if ( hits[ i ] < 0x100000 )
                {
                    pats.push_back( defs[ i ].sig );
                    map.push_back( i );
                }
            if ( !pats.empty( ) )
            {
                g_scanStage = 2;
                g_scanPermille = 0;
                LOG( "[update] %d signature(s) missing — full-image multi-scan...\n", (int)pats.size( ) );
                std::vector<uint64_t> h;
                TargetProcess->FindSignaturesMulti( pats, baseAddr + scan_window_rva, baseAddr + baseSize, h,
                    0x400000, [ & ]( size_t done, size_t total ) {
                        g_scanPermille = (int)( (uint64_t)done * 1000 / ( total ? total : 1 ) );
                    } );
                for ( size_t k = 0; k < map.size( ); ++k )
                    if ( h[ k ] >= 0x100000 )
                        hits[ map[ k ] ] = h[ k ];
            }
        }

        uint64_t c_game_sig   = hits[ 0 ];
        uint64_t c_local_sig  = hits[ 1 ];
        uint64_t dm_entry     = hits[ 2 ];
        uint64_t dm_get_hp    = hits[ 3 ];
        uint64_t dm_get_props = hits[ 4 ];
        uint64_t bullets_em   = hits[ 5 ];
        uint64_t vm_sig       = hits[ 6 ];
        uint64_t hud_sig      = hits[ 7 ];
        uint64_t opt_sig      = hits[ 8 ];
        uint64_t mu_sig       = hits[ 9 ];
        uint64_t va_sig       = hits[ 10 ];
        uint64_t cam_off_sig  = hits[ 11 ];
        uint64_t unit_enum_sig = hits[ 12 ];

        // ── 失败签名延迟重试（防打包器预热/动态卸载期的瞬态读失败）——多签名单遍 ──
        for ( int round = 1; round <= scan_retry_rounds; ++round )
        {
            const uint64_t vals[ 13 ] = { c_game_sig, c_local_sig, dm_entry, dm_get_hp, dm_get_props,
                bullets_em, vm_sig, hud_sig, opt_sig, mu_sig, va_sig, cam_off_sig, unit_enum_sig };
            std::vector<const char*> pats;
            std::vector<size_t> map;
            for ( size_t i = 0; i < 13; ++i )
                if ( vals[ i ] < 0x100000 )
                {
                    pats.push_back( defs[ i ].sig );
                    map.push_back( i );
                }
            const int missing = (int)pats.size( );
            if ( missing == 0 )
                break;
            LOG( "[update] %d signature(s) missing — retry %d/%d after %d ms...\n",
                 missing, round, scan_retry_rounds, scan_retry_delay_ms );
            std::this_thread::sleep_for( std::chrono::milliseconds( scan_retry_delay_ms ) );
            g_scanStage = 3;
            g_scanPermille = 0;
            std::vector<uint64_t> h;
            TargetProcess->FindSignaturesMulti( pats, baseAddr, baseAddr + baseSize, h,
                0x400000, [ & ]( size_t done, size_t total ) {
                    g_scanPermille = (int)( (uint64_t)done * 1000 / ( total ? total : 1 ) );
                } );
            for ( size_t k = 0; k < map.size( ); ++k )
                if ( h[ k ] >= 0x100000 )
                    hits[ map[ k ] ] = h[ k ];
            c_game_sig = hits[ 0 ];   c_local_sig = hits[ 1 ];
            dm_entry = hits[ 2 ];     dm_get_hp = hits[ 3 ];
            dm_get_props = hits[ 4 ]; bullets_em = hits[ 5 ];
            vm_sig = hits[ 6 ];       hud_sig = hits[ 7 ];
            opt_sig = hits[ 8 ];      mu_sig = hits[ 9 ];
            va_sig = hits[ 10 ];      cam_off_sig = hits[ 11 ];
            unit_enum_sig = hits[ 12 ];
        }

        // c_game → RVA
        uintptr_t c_game_rva = 0;
        if ( c_game_sig < 0x100000 )
        {
            LOG( "[update] get c_game failed!\n" );
        }
        else
        {
            c_game_rva = resolve_global_offset( c_game_sig );
            LOG( "[update] c_game sig @ 0x%llX, RVA = 0x%llX\n", c_game_sig, c_game_rva );
        }

        // c_local → RVA
        uintptr_t c_local_rva = 0;
        if ( c_local_sig < 0x100000 )
        {
            LOG( "[update] get c_local failed!\n" );
        }
        else
        {
            c_local_rva = resolve_global_offset( c_local_sig );
            LOG( "[update] c_local sig @ 0x%llX, RVA = 0x%llX\n", c_local_sig, c_local_rva );
        }

        // 其余全局根：解析 disp@+3/len7 → RVA → 运行时覆盖 offsets::globals::（均已改 inline）
        auto reloc = [ & ]( uint64_t sig, uintptr_t& field, const char* name ) -> uintptr_t
        {
            if ( sig < 0x100000 )
            {
                LOG( "[update] %s sig NOT found (keep current 0x%llX)\n", name, (uint64_t)field );
                return field;
            }
            const uintptr_t rva = resolve_global_offset( sig );   // disp32@+3, instr_len=7
            if ( rva >= 0x100000 && rva < baseSize )
            {
                field = rva;
                LOG( "[update] %s sig @ 0x%llX, RVA = 0x%llX\n", name, sig, (uint64_t)rva );
            }
            else
                LOG( "[update] %s resolve out of range (0x%llX), keep current\n", name, (uint64_t)rva );
            return field;
        };
        const uintptr_t view_matrix_rva = reloc( vm_sig,  offsets::globals::view_matrix, "view_matrix" );
        const uintptr_t hud_info_rva    = reloc( hud_sig, offsets::globals::hud_info,    "hud_info" );
        const uintptr_t game_optics_rva = reloc( opt_sig, offsets::globals::game_optics, "game_optics" );
        const uintptr_t my_unit_rva     = reloc( mu_sig,  offsets::globals::my_unit,     "my_unit" );
        const uintptr_t view_angles_rva = reloc( va_sig,  offsets::globals::view_angles, "view_angles" );

        // 结构体字段偏移重定位：与 resolve_global_offset 不同，这里直接取指令的 disp32 作字段偏移
        auto reloc_field = [ & ]( uint64_t sig, int dispPos, uintptr_t& field, const char* name ) -> uintptr_t
        {
            if ( sig < 0x100000 )
            {
                LOG( "[update] %s sig NOT found (keep current 0x%X)\n", name, (unsigned)field );
                return field;
            }
            const uint32_t d = TargetProcess->Read<uint32_t>( sig + dispPos );
            if ( d > 0 && d < 0x2000 )   // 合理结构体字段界（camera/单位链字段均 <0x2000）
            {
                field = d;
                LOG( "[update] %s = 0x%X (from sig @ 0x%llX)\n", name, d, sig );
            }
            else
                LOG( "[update] %s disp out of struct range (0x%X), keep current 0x%X\n", name, d, (unsigned)field );
            return field;
        };
        const uintptr_t camera_offset_val = reloc_field( cam_off_sig,   15, offsets::cgame_offsets::camera_offset, "camera_offset" );
        const uintptr_t unit_list_val     = reloc_field( unit_enum_sig, 17, offsets::cgame_offsets::unit_list_3,   "unit_list_3" );
        const uintptr_t unit_count_val    = reloc_field( unit_enum_sig, 23, offsets::cgame_offsets::unit_count_3,  "unit_count_3" );

        // DM 入口链特征码（直接验证 unit+0x10A8 / +0x370 偏移有效性）
        if ( dm_entry < 0x100000 )
            LOG( "[update] DM entry signature NOT found! unit+0x10A8 / +0x370 may have changed.\n" );
        else
            LOG( "[update] DM entry sig @ 0x%llX (aces.exe+0x%llX) — unit+0x10A8 → +0x370 confirmed\n",
                 dm_entry, dm_entry - baseAddr );

        // dm_get_part_hp
        if ( dm_get_hp < 0x100000 )
            LOG( "[update] dm_get_part_hp signature NOT found!\n" );
        else
            LOG( "[update] dm_get_part_hp @ 0x%llX (aces.exe+0x%llX)\n", dm_get_hp, dm_get_hp - baseAddr );

        // dm_get_damage_part_props
        if ( dm_get_props < 0x100000 )
            LOG( "[update] dm_get_damage_part_props signature NOT found!\n" );
        else
            LOG( "[update] dm_get_damage_part_props @ 0x%llX (aces.exe+0x%llX)\n", dm_get_props, dm_get_props - baseAddr );

        // 弹丸追踪：EM 全局（spawn 处 0x3C0 store + lea rcx,[EM] + typeHash + getter call）
        uintptr_t bullets_em_rva = 0;
        uintptr_t bullets_em_sig_rva = 0;   // 签名命中点 RVA（代码区，供指纹）；失败时置 0 走哨兵
        if ( bullets_em < 0x100000 )
        {
            // 签名失败 → 沿用当前值（硬编码或上一轮文件值），保证业务不断供且指纹文件完整
            bullets_em_rva = offsets::bullets::entity_manager;
            LOG( "[update] bullets EM signature NOT found! Using current RVA 0x%llX.\n",
                 (uint64_t)bullets_em_rva );
        }
        else
        {
            // lea rcx,[rip+disp32] @ hit+8（7 字节）：EM = hit + 15 + disp32，disp32 @ hit+11
            const int32_t disp = TargetProcess->Read<int32_t>( bullets_em + 11 );
            const uint64_t em = bullets_em + 15 + static_cast<uint64_t>( disp );
            bullets_em_rva = static_cast<uintptr_t>( em - baseAddr );
            if ( bullets_em_rva < 0x100000 || bullets_em_rva >= baseSize )
            {
                LOG( "[update] bullets EM sig resolve out of range (0x%llX), fallback hardcoded.\n",
                     (uint64_t)bullets_em_rva );
                bullets_em_rva = offsets::bullets::entity_manager;   // 回退，不写 0（0 会被文件加载覆盖业务值）
            }
            else
            {
                offsets::bullets::entity_manager = bullets_em_rva;
                bullets_em_sig_rva = static_cast< uintptr_t >( bullets_em - baseAddr );
                LOG( "[update] bullets EM sig @ 0x%llX, EM RVA = 0x%llX\n", bullets_em, (uint64_t)bullets_em_rva );
            }
        }

        // 写入 offsets 文件（dm_*/bullets_em_sig 存 RVA；指纹 = 各签名命中点原字节 FNV-1a）
        auto to_rva = [ & ]( uint64_t addr ) -> uintptr_t
        {
            return ( addr >= 0x100000 && addr >= baseAddr ) ? static_cast< uintptr_t >( addr - baseAddr ) : 0;
        };
        std::unordered_map<std::string, uintptr_t> fp_map = {
            { "dm_entry", to_rva( dm_entry ) },
            { "dm_get_hp", to_rva( dm_get_hp ) },
            { "dm_get_props", to_rva( dm_get_props ) },
            { "bullets_em_sig", bullets_em_sig_rva },
            { "cam_off_sig", to_rva( cam_off_sig ) },
            { "unit_enum_sig", to_rva( unit_enum_sig ) },
        };
        const uint64_t fingerprint = compute_fingerprint( fp_map );
        // ★关键签名守卫（2026-10-06）：游戏保护器会定期重新加密未执行的代码页，
        //   扫描可能在页面密文期进行→关键签名全失。此时绝不能用残缺结果
        //   覆盖上一份好的 offsets 文件（否则下次启动丢失 c_game 行必然重扫）。
        if ( c_game_rva < 0x100000 || c_local_rva < 0x100000 )
        {
            LOG( "[update] critical sigs missing (c_game/c_local) — offsets file NOT overwritten.\n" );
            LOG( "[update] background self-heal will retry as pages decode.\n" );
            return false;
        }
        write_offsets_file( fingerprint, c_game_rva, c_local_rva,
            fp_map[ "dm_entry" ], fp_map[ "dm_get_hp" ], fp_map[ "dm_get_props" ],
            bullets_em_rva, bullets_em_sig_rva,
            { { "view_matrix", view_matrix_rva }, { "hud_info", hud_info_rva },
              { "game_optics", game_optics_rva }, { "my_unit", my_unit_rva },
              { "view_angles", view_angles_rva },
              { "cam_off_sig", to_rva( cam_off_sig ) }, { "unit_enum_sig", to_rva( unit_enum_sig ) } },
            { { "camera_offset", camera_offset_val }, { "unit_list_3", unit_list_val },
              { "unit_count_3", unit_count_val } } );

        // 应用全局偏移到 offsets 命名空间
        if ( c_game_rva >= 0x100000 )
            offsets::globals::game_context = c_game_rva;
        if ( c_local_rva >= 0x100000 )
            offsets::globals::local_player = c_local_rva;

        LOG( "[update] Signature scan complete, fingerprint %016llX. Offsets written to file.\n", fingerprint );
        return true;
    }

    // ── 主入口（★2026-10-06 拆分）──────────────────────────────────
    // try_load_cached：指纹快路（命中→直接加载缓存偏移，同步零感知）。
    // kick：快路命中同步初始化并置 g_ready；未命中则后台线程执行扫描+onReady（SDK 初始化），
    //      期间渲染循环与菜单照常响应，overlay 显示校准进度（替代启动白屏无响应）。
    // 原理：offsets 文件存上一轮各签名命中点 RVA；启动时读这些点的当前字节做 FNV-1a 比对指纹。
    inline auto try_load_cached( ) -> bool
    {
        if ( std::filesystem::exists( "offsets" ) )
        {
            auto offset_map = parse_offsets( );
            if ( offset_map.count( "c_game" ) && offset_map.count( "bullets_em" ) )
            {
                const uint64_t fp_now = compute_fingerprint( offset_map );
                auto it = offset_map.find( "fingerprint" );
                if ( it != offset_map.end( ) && it->second == fp_now )
                {
                    offsets::globals::game_context = offset_map[ "c_game" ];
                    // c_local 缺键时（该轮签名失配，文件不写该行）保留代码内常量兜底
                    const auto itLocal = offset_map.find( "c_local" );
                    if ( itLocal != offset_map.end( ) && itLocal->second >= 0x100000 )
                        offsets::globals::local_player = itLocal->second;
                    offsets::bullets::entity_manager = offset_map[ "bullets_em" ];
                    auto loadg = [ & ]( const char* k, uintptr_t& f )
                    {
                        const auto it = offset_map.find( k );
                        if ( it != offset_map.end( ) && it->second >= 0x100000 ) f = it->second;
                    };
                    loadg( "view_matrix", offsets::globals::view_matrix );
                    loadg( "hud_info",    offsets::globals::hud_info );
                    loadg( "game_optics", offsets::globals::game_optics );
                    loadg( "my_unit",     offsets::globals::my_unit );
                    loadg( "view_angles", offsets::globals::view_angles );
                    auto loadsf = [ & ]( const char* k, uintptr_t& f )
                    {
                        const auto it = offset_map.find( k );
                        if ( it != offset_map.end( ) && it->second > 0 && it->second < 0x2000 ) f = it->second;
                    };
                    loadsf( "camera_offset", offsets::cgame_offsets::camera_offset );
                    loadsf( "unit_list_3",   offsets::cgame_offsets::unit_list_3 );
                    loadsf( "unit_count_3",  offsets::cgame_offsets::unit_count_3 );
                    LOG( "[update] Fingerprint MATCH (0x%016llX) — cached offsets loaded:\n"
                         "[update]   c_game=0x%llX c_local=0x%llX bullets_em=0x%llX\n",
                         fp_now,
                         (uint64_t)offsets::globals::game_context, (uint64_t)offsets::globals::local_player,
                         (uint64_t)offsets::bullets::entity_manager );
                    return true;
                }
                LOG( "[update] Fingerprint MISMATCH (stored=%s, now=0x%016llX) — rescanning...\n",
                     it != offset_map.end( ) ? "differs" : "absent", fp_now );
            }
            else
                LOG( "[update] offsets file incomplete — rescanning...\n" );
        }
        else
            LOG( "[update] No offsets file found — performing initial scan...\n" );
        return false;
    }

    // onReady：扫描+SDK 初始化完成后的回调（由拥有 sdk 可见性的调用方提供，内部做
    //          sdk::init + g_ready=true）。返回 true=快路已就绪（调用方自己调 onReady）；
    //          false=后台校准中，完成后自动调 onReady。
    inline auto kick( const std::function<void( )>& onReady ) -> bool
    {
        LOG( "[update] Layout-fingerprint offset validation...\n" );
        if ( try_load_cached( ) )
            return true;

        g_scanStage = 1;
        std::thread( [ onReady ]( ) {
            try
            {
                // ★自愈循环：保护器定期重加密未执行代码页，扫描可能在密文期失败。
                //   每 60s 重试（页面被执行时解码），最多 10 轮；期间业务用内置常量正常运行。
                for ( int heal = 0; heal < 10; ++heal )
                {
                    if ( do_scan( ) )
                        break;
                    LOG( "[update] self-heal %d/10 after 60s (code pages re-encrypted by protector)\n", heal + 1 );
                    std::this_thread::sleep_for( std::chrono::seconds( 60 ) );
                }
                g_scanStage = 4;
                if ( onReady )
                    onReady( );
                g_scanStage = 5;
                g_ready = true;
                LOG( "[update] background calibration done — ESP online\n" );
            }
            catch ( ... )
            {
                g_calibFailed = true;
            }
        } ).detach( );
        return false;
    }
}
