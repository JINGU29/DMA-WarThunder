# DMA-WarThunder-SDK 优化方案

## 概述

根据 `un.md` 的优化建议，对现有代码进行 4 项优化，提升 DMA 读取性能和渲染效率。

## 优化架构总览

```
GameUpdate 线程（100ms）              渲染线程（每帧）
┌───────────────────────────┐       ┌───────────────────────┐
│ 1. 检查缓存，按需刷新指针   │       │                       │
│    (local player / cGame)  │       │  读取预计算数据         │
│ 2. Scatter read 所有 unit  │──mutex│  (拷贝一份 SImGuiUnit)  │
│    全部字段（一次性 batch） │       │                       │
│ 3. 预计算屏幕坐标、AABB     │       │  遍历绘制 ESP          │
│ 4. 如开启则做弹道预测        │       │  如开启则绘制瞄准点     │
│ 5. 写入共享数据（加锁）     │       │                       │
└───────────────────────────┘       └───────────────────────┘
```

## 优化 1：缓存不常变化的数据（优化 A）

**目标**：减少对 `local player` 指针和 `cGame` 指针的频繁读取。

**方案**：在任何非存活/非观战状态时重新获取 `local player` 和 `cGame` 指针。

**修改文件**：
- `src/game/classes/entity.hpp` — `c_entity` 增加 `needs_reinit()` 方法
- `src/game/classes/game.hpp` — `c_game` 增加 `needs_reinit()` 方法
- `src/core/core.hpp` — `GameUpdate` 线程中检查状态并按需重新初始化

**逻辑**：
```
if (gui_state != ALIVE && gui_state != SPEC) {
    cLocalPlayer->init();  // 重新获取 local player 指针
    cGame->init();          // 重新获取 cGame 指针
    cGame->ballistics->init(cGame->get_base());
    cGame->camera->init(cGame->get_base());
}
```

## 优化 2：Scatter Reads 批量读取（优化 B）

**目标**：对所有 unit 的所有字段一次性 scatter read，最大化减少 DMA 通信开销。

**方案**：在 `GameUpdate` 线程中，先收集所有 unit 的所有字段读取请求到一个 scatter handle，然后一次性执行。

**每个 unit 需要读取的字段**：
- `position` (vec3_t, offset: 0xD08)
- `bbmin` (vec3_t, offset: 0x240)
- `bbmax` (vec3_t, offset: 0x24C)
- `rotation_matrix` (matrix3x4_t, offset: 0xCE4)
- `unitState` (uint16_t, offset: 0xF60)
- `team` (uint8_t, offset: 0xFE0)
- `groundmovement` 指针 → `velocity` (vec3_t)
- `info` 指针 → `unitType` (string)

## 优化 3：分离渲染和游戏更新线程（优化 C）

**目标**：后台线程维持 100ms 更新间隔，预计算所有 ESP + aimbot 所需数据，渲染线程只读取最终结果绘制。

**新增数据结构**：
```cpp
struct AABB {
    vec3_t m_min;
    vec3_t m_max;
    AABB() : m_min(vec3_t(0,0,0)), m_max(vec3_t(0,0,0)) {}
    AABB(vec3_t min, vec3_t max) : m_min(min), m_max(max) {}
};

struct SImGuiVert {
    bool bOnScreen{false};
    vec2_t origin;
};

struct SImGuiGame {
    uintptr_t cGame;
    uintptr_t cCamera;
    matrix4x4_t viewMatrix;
};

struct SImGuiUnit {
    bool bOnScreen{false};
    bool bValidEnemy{false};
    uintptr_t unitAddr{0};
    vec3_t worldOrigin;
    AABB worldBounds;
    matrix3x4_t rotation;
    vec2_t screenOrigin;
    std::array<SImGuiVert, 8> screenBoxVerts;
    float distance{0.0f};
    uint8_t team{0};
    uint16_t unitState{0};
    // aimbot 预计算
    vec3_t velocity;
    vec3_t aimPoint;       // 弹道预测点（仅 aimbot 开启时计算）
    bool bHasAimPoint{false};
};

struct SGameData {
    SImGuiGame gameCtx;
    std::vector<SImGuiUnit> units;
    vec3_t localPosition;
    bool bIsValid{false};
};
```

**修改文件**：
- `src/game/datatypes/` — 新增数据结构定义
- `src/features/misc/misc.hpp` — 重构 `UpdateEntityList` → `GameUpdate`
- `src/features/esp/esp.hpp` — 渲染线程只读取预计算数据
- `src/core/core.hpp` — 调整线程结构

## 优化 4：线程安全的实体列表（优化 D）

**目标**：使用 `std::mutex` 互斥锁保护共享数据。

**方案**：
- 后台线程写入 `SGameData` 时加锁
- 渲染线程读取时加锁，然后拷贝一份本地副本用于渲染

```cpp
// 共享数据
inline std::mutex g_gameMutex;
inline SGameData g_gameData;

// GameUpdate 线程写入
{
    std::lock_guard<std::mutex> lock(g_gameMutex);
    g_gameData = computedData;
}

// 渲染线程读取
SGameData localData;
{
    std::lock_guard<std::mutex> lock(g_gameMutex);
    localData = g_gameData;
}
// 使用 localData 进行渲染...
```

## aimbot 折中方案

- `GameUpdate` 线程预读取全部数据（包括 aimbot 字段如 `velocity`）
- 弹道预测计算通过 `bool bAimbotEnabled` 开关控制
- 不开启时跳过弹道计算，不增加额外 DMA 开销
- 额外数据量很小（约 20 字节/unit），在同一个 scatter batch 中不增加额外通信次数

## 修改文件清单

| 文件 | 修改内容 |
|------|---------|
| `src/game/datatypes/game_data.hpp` | 新增 AABB/SImGuiVert/SImGuiGame/SImGuiUnit/SGameData 结构 |
| `src/game/classes/entity.hpp` | 增加状态检查方法 |
| `src/game/classes/game.hpp` | 增加重初始化方法 |
| `src/features/misc/misc.hpp` | 重构为 GameUpdate，实现全量 scatter read + 预计算 |
| `src/features/esp/esp.hpp` | 渲染线程改为纯绘制 |
| `src/features/aimbot/aimbot.hpp` | 适配预计算数据 |
| `src/core/core.hpp` | 调整线程结构，增加缓存检查 |
| `src/game/classes/camera.hpp` | 适配 scatter 预读取 |

## 实施顺序

1. 新增数据结构（`game_data.hpp`）
2. 缓存策略优化（`c_game` / `c_entity` / `core.hpp`）
3. 重构 `GameUpdate`（scatter read + 预计算 + mutex）
4. 重构 `esp::run()`（纯绘制）
5. 适配 `aimbot`
6. 测试编译
