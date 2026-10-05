#pragma once

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
    // ── 特征码定义 ──────────────────────────────────────────────
    // c_game: mov rax, [rip+disp32] → 48 8B 05 ? ? ? ? F2 0F 10 4F
    constexpr const char* sig_c_game      = "48 8B 05 ? ? ? ? F2 0F 10 4F";
    // c_local: cmp [rip+disp32], rcx → 48 39 0D ? ? ? ? 75 ? 48 C7 05
    constexpr const char* sig_c_local     = "48 39 0D ? ? ? ? 75 ? 48 C7 05";
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
        return h;
    }

    // ── 写入 offsets 文件 ──────────────────────────────────────
    inline auto write_offsets_file( uint64_t fingerprint, uintptr_t c_game_rva, uintptr_t c_local_rva,
                                     uint64_t dm_entry_rva, uint64_t dm_get_hp_rva, uint64_t dm_get_props_rva,
                                     uintptr_t bullets_em_rva, uintptr_t bullets_em_sig_rva ) -> void
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

    // ── 执行特征码扫描并写入 offsets 文件 ──────────────────────
    inline auto do_scan( ) -> void
    {
        LOG( "[update] Starting full signature scan...\n" );

        // c_game
        auto c_game_sig = TargetProcess->FindSignature( sig_c_game, baseAddr, baseAddr + baseSize );
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

        // c_local
        auto c_local_sig = TargetProcess->FindSignature( sig_c_local, baseAddr, baseAddr + baseSize );
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

        // DM 入口链特征码（直接验证 unit+0x10A8 / +0x370 偏移有效性）
        auto dm_entry = TargetProcess->FindSignature( sig_dm_entry, baseAddr, baseAddr + baseSize );
        if ( dm_entry < 0x100000 )
            LOG( "[update] DM entry signature NOT found! unit+0x10A8 / +0x370 may have changed.\n" );
        else
            LOG( "[update] DM entry sig @ 0x%llX (aces.exe+0x%llX) — unit+0x10A8 → +0x370 confirmed\n",
                 dm_entry, dm_entry - baseAddr );

        // dm_get_part_hp
        auto dm_get_hp = TargetProcess->FindSignature( sig_dm_get_hp, baseAddr, baseAddr + baseSize );
        if ( dm_get_hp < 0x100000 )
            LOG( "[update] dm_get_part_hp signature NOT found!\n" );
        else
            LOG( "[update] dm_get_part_hp @ 0x%llX (aces.exe+0x%llX)\n", dm_get_hp, dm_get_hp - baseAddr );

        // dm_get_damage_part_props
        auto dm_get_props = TargetProcess->FindSignature( sig_dm_get_props, baseAddr, baseAddr + baseSize );
        if ( dm_get_props < 0x100000 )
            LOG( "[update] dm_get_damage_part_props signature NOT found!\n" );
        else
            LOG( "[update] dm_get_damage_part_props @ 0x%llX (aces.exe+0x%llX)\n", dm_get_props, dm_get_props - baseAddr );

        // 弹丸追踪：EM 全局（spawn 处 0x3C0 store + lea rcx,[EM] + typeHash + getter call）
        auto bullets_em = TargetProcess->FindSignature( sig_bullets_em, baseAddr, baseAddr + baseSize );
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
        };
        const uint64_t fingerprint = compute_fingerprint( fp_map );
        write_offsets_file( fingerprint, c_game_rva, c_local_rva,
            fp_map[ "dm_entry" ], fp_map[ "dm_get_hp" ], fp_map[ "dm_get_props" ],
            bullets_em_rva, bullets_em_sig_rva );

        // 应用全局偏移到 offsets 命名空间
        if ( c_game_rva >= 0x100000 )
            offsets::globals::game_context = c_game_rva;
        if ( c_local_rva >= 0x100000 )
            offsets::globals::local_player = c_local_rva;

        LOG( "[update] Signature scan complete, fingerprint %016llX. Offsets written to file.\n", fingerprint );
    }

    // ── 主入口：布局指纹门控 ──────────────────────────────────
    // 不再依赖版本字符串（实测本版映像内无 ASCII 版本串，且模块尾部数据区会被动态卸载）。
    // 原理：offsets 文件存有上一轮各签名命中点 RVA；启动时读这些点的当前字节做 FNV-1a，
    //       与存档指纹一致 → 偏移仍有效直接加载；不一致（游戏热更/文件缺失/老格式）→ 全量重扫。
    inline auto run( ) -> bool
    {
        LOG( "[update] Layout-fingerprint offset validation...\n" );
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
                    offsets::globals::local_player = offset_map[ "c_local" ];
                    offsets::bullets::entity_manager = offset_map[ "bullets_em" ];
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

        do_scan( );
        LOG( "Offsets loaded for War Thunder (fingerprint-validated)\n" );
        return true;
    }
}
