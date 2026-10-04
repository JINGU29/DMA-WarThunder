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

    // 版本字符串在 aces.exe 中的偏移（2.59.0.44 实测）
    // 若版本变化，此偏移可能也需要调整，但 bditt 原版一直用这个
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

    // ── 写入 offsets 文件 ──────────────────────────────────────
    inline auto write_offsets_file( const std::string& version, uintptr_t c_game_rva, uintptr_t c_local_rva,
                                     uint64_t dm_entry_addr, uint64_t dm_get_hp_addr, uint64_t dm_get_props_addr ) -> void
    {
        std::ofstream outFile( "offsets", std::ios::trunc );
        if ( !outFile.is_open( ) )
            return;

        outFile << version << std::endl;

        // 全局指针偏移（RVA）
        if ( c_game_rva < 0x100000 )
            outFile << "get c_game failed!" << std::endl;
        else
            outFile << "c_game: 0x" << std::hex << c_game_rva << std::dec << std::endl;

        if ( c_local_rva < 0x100000 )
            outFile << "get c_local failed!" << std::endl;
        else
            outFile << "c_local: 0x" << std::hex << c_local_rva << std::dec << std::endl;

        // DM 特征码命中地址（绝对地址，用于日志/验证）
        outFile << "dm_entry: 0x" << std::hex << dm_entry_addr << std::dec << std::endl;
        outFile << "dm_get_hp: 0x" << std::hex << dm_get_hp_addr << std::dec << std::endl;
        outFile << "dm_get_props: 0x" << std::hex << dm_get_props_addr << std::dec << std::endl;

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

        // 跳过版本行
        std::getline( file, line );

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
    inline auto do_scan( const std::string& version ) -> void
    {
        LOG( "[update] Starting signature scan for version %s...\n", version.c_str( ) );

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

        // 写入 offsets 文件
        write_offsets_file( version, c_game_rva, c_local_rva, dm_entry, dm_get_hp, dm_get_props );

        // 应用全局偏移到 offsets 命名空间
        if ( c_game_rva >= 0x100000 )
            offsets::globals::game_context = c_game_rva;
        if ( c_local_rva >= 0x100000 )
            offsets::globals::local_player = c_local_rva;

        LOG( "[update] Signature scan complete. Offsets written to file.\n" );
    }

    // ── 主入口 ─────────────────────────────────────────────────
    inline auto run( ) -> bool
    {
        // 读取当前游戏版本字符串
        std::string current_version = TargetProcess->ReadString( baseAddr + version_string_rva );
        if ( current_version.empty( ) )
        {
            LOG( "[update] Warning: could not read version string, using hardcoded offsets.\n" );
            LOG( "Offsets loaded for War Thunder (hardcoded fallback)\n" );
            return true;
        }

        LOG( "[update] Game version: %s\n", current_version.c_str( ) );

        // 检查 offsets 文件是否存在
        if ( std::filesystem::exists( "offsets" ) )
        {
            std::string cached_version;
            std::ifstream file( "offsets" );
            if ( file.is_open( ) )
            {
                std::getline( file, cached_version );
                file.close( );
            }

            if ( cached_version == current_version )
            {
                // 版本一致，从文件加载偏移
                auto offset_map = parse_offsets( );

                if ( offset_map.find( "c_game" ) != offset_map.end( ) )
                    offsets::globals::game_context = offset_map[ "c_game" ];

                if ( offset_map.find( "c_local" ) != offset_map.end( ) )
                    offsets::globals::local_player = offset_map[ "c_local" ];

                LOG( "[update] Version match — loaded cached offsets from file.\n" );
                LOG( "[update] c_game RVA = 0x%llX, c_local RVA = 0x%llX\n",
                     (uint64_t)offsets::globals::game_context, (uint64_t)offsets::globals::local_player );

                // 即使版本一致，也验证 DM 入口链特征码是否仍然有效
                auto dm_entry = TargetProcess->FindSignature( sig_dm_entry, baseAddr, baseAddr + baseSize );
                if ( dm_entry >= 0x100000 )
                    LOG( "[update] DM entry chain confirmed (unit+0x10A8 → +0x370) @ aces.exe+0x%llX\n", dm_entry - baseAddr );
                else
                    LOG( "[update] WARNING: DM entry signature not found! Damage model offsets may need update.\n" );

                return true;
            }

            // 版本不匹配，执行重新扫描
            LOG( "[update] Version changed (%s → %s), rescanning...\n", cached_version.c_str( ), current_version.c_str( ) );
        }
        else
        {
            LOG( "[update] No offsets file found, performing initial scan...\n" );
        }

        // 执行特征码扫描
        do_scan( current_version );

        LOG( "Offsets loaded for War Thunder %s\n", current_version.c_str( ) );
        return true;
    }
}
