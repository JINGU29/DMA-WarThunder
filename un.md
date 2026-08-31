decent source , could use some optimization but otherwise should be good enough for people to learn from , thanks for sharing!

To expand on what could be optimized ( in case you were looking for feedback on how to improve)
- cache data that doesn't change often ( local player pointer , cgame ) maybe grab these each time the player dies for instance. This will reduce overhead on the FPGA device as you won't be requesting data as often which should give a slight performance boost.
- implement scatter reads
- offload memory operations from the render thread ( inside render you do a lot of memory operations like reading data especially in a loop as well as world to screen calls which has a lot of math and can slow down rendering)


example render optimization, the following should be done in a separate thread (GameUpdate) , then you can fetch each unit that is on screen and immediately begin rendering
Code:
inline auto Think( ) -> void 
{
    uintptr_t cGame = TargetProcess->Read< uintptr_t >( baseAddr + offsets::cgame_offset );
    uintptr_t cCamera = TargetProcess->Read< uintptr_t >( cGame + offsets::cgame_offsets::camera_offset );
    ViewMatrix_t matrix = TargetProcess->Read< ViewMatrix_t >( cCamera + offsets::cgame_offsets::camera_offsets::camera_matrix_offset );
 
	for ( uintptr_t unit : misc::units )
	{
        vec3_t unitPosition = TargetProcess->Read< vec3_t >( unit + 0xAE8 );
		if ( unitPosition.empty( ) )
			continue;
 
        vec2_t screenPosition;
		if ( !g_render->world_to_screen( unitPosition, screenPosition, matrix ) )
			continue;
}
example code refactor
inside of misc.hpp you have a function to update the entity list. There is a variable "inline std::vector< uintptr_t > units" which is used as a storage container for the base address of each entity. before grabbing new units you clear the vector ... if you follow my advice up top and seperate threads ( Render , GameUpdate ) you will need to adjust the way you get a list of entities. Right now before retrieving the updated list you call "Clear()" on the vector ... which would clear whatever is currently being rendered (you grab units directly , instead grab a copy of units so that if and when it changes ... on the next render frame you will grab a copy of the new data)

Code:
// instead of doing
for ( uintptr_t unit : misc::units ) { // logic }
 
// do the following
const auto units = misc::units; // creates a local copy of the container
for ( uintptr_t unit : units ) { // logic }
Code:
inline std::vector< uintptr_t > units;
inline auto UpdateEntityList( ) -> void 
{
// implement scatter reads
	uintptr_t local = TargetProcess->Read< uintptr_t >( baseAddr + offsets::localplayer_offset );
	uintptr_t cGame = TargetProcess->Read< uintptr_t >( baseAddr + offsets::cgame_offset );
	if ( !local || !cGame )
	{
		LOG( "offsets outdated...\n" );
		return;
	}
 
	uint8_t localGuiState = TargetProcess->Read< uint8_t >( local + offsets::localplayer::guiState_offset );
	if ( localGuiState != 2 && localGuiState != 6 )
		return;
 
	uintptr_t localUnit = TargetProcess->Read< uintptr_t >( local + offsets::localplayer::localunit_offset ) - 1;
	uintptr_t unitList3 = TargetProcess->Read< uintptr_t >( cGame + 0x340 );
	uint16_t unitCount3 = TargetProcess->Read< uint16_t >( cGame + 0x350 );
 
	uint8_t localTeam = TargetProcess->Read< uint8_t >( localUnit + offsets::unit_offsets::unitArmyNo_offset );
 
	units.clear( ); // <- try not to do this
	for ( int i = 0; i < static_cast< int >( unitCount3 ); ++i ) { // implement scatter reads }
}


additionally you could just overwrite the vector container as opposed to clearing the data ( copying an empty vector container will provide the very same result )
Code:
inline std::vector< uintptr_t > units;
inline auto UpdateEntityList( ) -> void 
{
	std::vector<uintptr_t> result; // populate the result
	// perform the same logic as above to get the unit array , though try to implement scatter reads
 
	for ( int i = 0; i < static_cast< int >( unitCount3 ); ++i ) 
	{ 
		// implement scatter reads 
		result.push_back(unitAddr); // <- pushing unit into result container
	}
	
	units = result; // <- set units to result
example scatter reads
Code:
inline auto UpdateEntityList( ) -> void 
{
	VMMDLL_SCATTER_HANDLE hScatter = TargetProcess->CreateScatterHandle();
	if (!hScatter)
		return;
 
	uintptr_t cGame = 0;
	uintptr_t local = 0;
	if (!TargetProcess->AddScatterReadRequest(hScatter, uint64_t(baseAddr + offsets::cgame_offset), &cGame, 0x08)
		|| TargetProcess->AddScatterReadRequest(hScatter, uint64_t(baseAddr + offsets::cgame_offset), &local, 0x08))
		return;
 
	if (!TargetProcess->ExecuteReadScatter(hScatter))
		return;
 
	if (!cGame || !local)
	{
		LOG("offsets outdated...\n");
		return;
	}
youll need to add an additional function to "c_memory" for clearing the scatter handle , I also updated some of the c_memory functions to have a boolean return value
Code:
bool c_memory::ClearScatterHandle(VMMDLL_SCATTER_HANDLE handle)
{
	if (!VMMDLL_Scatter_Clear(handle, this->current_process.PID, VMMDLL_FLAG_NOCACHE))
	{
		LOG("[-] Failed to clear Scatter\n");
		return false;
	}
 
	return true;
}

updated c_memory scatter functions
Code:
bool c_memory::ClearScatterHandle(VMMDLL_SCATTER_HANDLE handle)
{
	if (!VMMDLL_Scatter_Clear(handle, this->current_process.PID, VMMDLL_FLAG_NOCACHE))
	{
		LOG("[-] Failed to clear Scatter\n");
		return false;
	}
 
	return true;
}
 
bool c_memory::AddScatterReadRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size)
{
	DWORD memoryPrepared = NULL;
	if (!VMMDLL_Scatter_PrepareEx(handle, address, size, (PBYTE)buffer, &memoryPrepared))
	{
	//	LOG("[!] Failed to prepare scatter read at 0x%p\n", address);
		return false;
	}
	return true;
}
 
bool c_memory::AddScatterWriteRequest(VMMDLL_SCATTER_HANDLE handle, uint64_t address, void* buffer, size_t size)
{
	if (!(address > 0x2000000 && address < 0x7FFFFFFFFFFF))
		return false;
	if (!VMMDLL_Scatter_PrepareWrite(handle, address, (PBYTE)buffer, size))
	{
		//LOG("[!] Failed to prepare scatter write at 0x%p\n", address);
		return false;
	}
 
	return true;
}
 
bool c_memory::ExecuteScatterWrite(VMMDLL_SCATTER_HANDLE handle, bool bClear)
{
 
	if (!VMMDLL_Scatter_Execute(handle))
	{
	//	LOG("[-] Failed to Execute Scatter Read\n");
		return false;
	}
	//Clear after using it
	if (bClear && !VMMDLL_Scatter_Clear(handle, this->current_process.PID, VMMDLL_FLAG_NOCACHE))
	{
	//	LOG("[-] Failed to clear Scatter\n");
		return false;
	}
 
	return true;
}
 
bool c_memory::ExecuteScatterRead(VMMDLL_SCATTER_HANDLE handle, bool bClear)
{
 
	if (!VMMDLL_Scatter_ExecuteRead(handle))
	{
		LOG("[-] Failed to Execute Scatter Read\n");
		return false;
	}
	//Clear after using it
	if (bClear && !VMMDLL_Scatter_Clear(handle, this->current_process.PID, VMMDLL_FLAG_NOCACHE))
	{
		LOG("[-] Failed to clear Scatter\n");
		return false;
	}
	return true;
}
 
bool c_memory::ExecuteReadScatter(VMMDLL_SCATTER_HANDLE handle, int pid, bool bClear)
{
	if (pid == 0)
		pid = this->current_process.PID;
 
	if (!VMMDLL_Scatter_ExecuteRead(handle))
	{
		LOG("[-] Failed to Execute Scatter Read\n");
		return false;
	}
	//Clear after using it
	if (bClear && !VMMDLL_Scatter_Clear(handle, pid, VMMDLL_FLAG_NOCACHE))
	{
		LOG("[-] Failed to clear Scatter\n");
		return false;
	}
 
	return true;
}
 
bool c_memory::ExecuteWriteScatter(VMMDLL_SCATTER_HANDLE handle, int pid, bool bClear)
{
	if (pid == 0)
		pid = this->current_process.PID;
 
	if (!VMMDLL_Scatter_Execute(handle))
	{
		LOG("[-] Failed to Execute Scatter Read\n");
		return false;
	}
	//Clear after using it
	if (bClear && !VMMDLL_Scatter_Clear(handle, pid, VMMDLL_FLAG_NOCACHE))
	{
		LOG("[-] Failed to clear Scatter\n");
		return false;
	}
 
	return true;
}

Heres a complete example on what i mean by scatter reads & caching data. I made a method for getting the game context ( game, camera & view matrix ) as well as each unit
The method "ScatterUnit" could be expanded to scatter read request ALL units BEFORE executing the read request to provide an even greater performance boost
Code:
struct AABB
{
	vec3_t m_min;
	vec3_t m_max;
 
	AABB() : m_min(vec3_t(0.0f, 0.0f, 0.0f)), m_max(vec3_t(0.0f, 0.0f, 0.0f)) {}
	AABB(vec3_t min, vec3_t max) : m_min(min), m_max(max) {}
 
};
 
struct SImGuiVert
{
	bool bOnScreen{ false };
	vec2_t origin;
};
 
struct SImGuiGame
{
	uintptr_t cGame;
	uintptr_t cCamera;
	ViewMatrix_t viewMatrix;
};
 
struct SImGuiUnit
{
	bool bOnScreen{ false };
	vec3_t worldOrigin;
	AABB worldBounds;
	vec2_t screenOrigin;
	std::vector<SImGuiVert> screenBoxVerts;
};
 
inline auto ScatterGame(uintptr_t gameAddr, SImGuiGame& outCtx) -> bool
{
	VMMDLL_SCATTER_HANDLE hScatter = TargetProcess->CreateScatterHandle();
	if (!hScatter)
		return false;
 
	/* scatter request & read cgame */
	uintptr_t cGame;
	if (!TargetProcess->AddScatterReadRequest(hScatter, uint64_t(baseAddr + offsets::cgame_offset), &cGame, sizeof(cGame))
		|| !TargetProcess->ExecuteReadScatter(hScatter, 0, true)
		|| !cGame)
	{
		TargetProcess->CloseScatterHandle(hScatter);
		return false;
	}
 
	/* scatter request & read ccamera */
	uintptr_t cCamera;
	if (!TargetProcess->AddScatterReadRequest(hScatter, uint64_t(cGame + offsets::cgame_offsets::camera_offset), &cCamera, sizeof(cCamera))
		|| !TargetProcess->ExecuteReadScatter(hScatter, 0, true)
		|| !cCamera)
	{
		TargetProcess->CloseScatterHandle(hScatter);
		return false;
	}
 
	/* scatter request & read viewmatrix */
	ViewMatrix_t matrix;
	if (!TargetProcess->AddScatterReadRequest(hScatter, uint64_t(cCamera + offsets::cgame_offsets::camera_offsets::camera_matrix_offset), &matrix, sizeof(ViewMatrix_t))
		|| !TargetProcess->ExecuteReadScatter(hScatter, 0, true))
	{
		TargetProcess->CloseScatterHandle(hScatter);
		return false;
	}
 
	TargetProcess->CloseScatterHandle(hScatter);
 
	outCtx.cGame = cGame;
	outCtx.cCamera = cCamera;
	outCtx.viewMatrix = matrix;
	return true;
}
 
inline auto ScatterUnit(uintptr_t unitAddr, const SImGuiGame& ctx, SImGuiUnit& outUnit) -> bool
{
	VMMDLL_SCATTER_HANDLE hScatter = TargetProcess->CreateScatterHandle();
	if (!hScatter)
		return false;
 
	vec3_t worldOrigin;
	AABB worldBounds;
	if (!TargetProcess->AddScatterReadRequest(hScatter, uint64_t(unitAddr + 0xAE8), &worldOrigin, sizeof(vec3_t)) 
		|| !TargetProcess->AddScatterReadRequest(hScatter, uint64_t(unitAddr + offsets::unit_offsets::bbmin_offset), &worldBounds, sizeof(AABB))
		|| !TargetProcess->ExecuteReadScatter(hScatter, 0, true))
	{
		TargetProcess->CloseScatterHandle(hScatter);
		return false;
	}
	TargetProcess->CloseScatterHandle(hScatter);
 
	std::vector<vec3_t> boxVerts = {
		vec3_t(worldOrigin.x + worldBounds.m_min.x, worldOrigin.y + worldBounds.m_min.y, worldOrigin.z + worldBounds.m_min.z), // 0
		vec3_t(worldOrigin.x + worldBounds.m_max.x, worldOrigin.y + worldBounds.m_min.y, worldOrigin.z + worldBounds.m_min.z), // 1
		vec3_t(worldOrigin.x + worldBounds.m_min.x, worldOrigin.y + worldBounds.m_max.y, worldOrigin.z + worldBounds.m_min.z), // 2
		vec3_t(worldOrigin.x + worldBounds.m_max.x, worldOrigin.y + worldBounds.m_max.y, worldOrigin.z + worldBounds.m_min.z), // 3
		vec3_t(worldOrigin.x + worldBounds.m_min.x, worldOrigin.y + worldBounds.m_min.y, worldOrigin.z + worldBounds.m_max.z), // 4
		vec3_t(worldOrigin.x + worldBounds.m_max.x, worldOrigin.y + worldBounds.m_min.y, worldOrigin.z + worldBounds.m_max.z), // 5
		vec3_t(worldOrigin.x + worldBounds.m_min.x, worldOrigin.y + worldBounds.m_max.y, worldOrigin.z + worldBounds.m_max.z), // 6
		vec3_t(worldOrigin.x + worldBounds.m_max.x, worldOrigin.y + worldBounds.m_max.y, worldOrigin.z + worldBounds.m_max.z)  // 7
	};
 
	/* get screen box verts */
	bool bVertOnScreen[8] = { false };
	std::vector<SImGuiVert> screenVerts(8);
	for (int i = 0; i < 8; i++)
		screenVerts[i].bOnScreen = g_render->world_to_screen(boxVerts[i], screenVerts[i].origin, ctx.viewMatrix);
 
	/* result */
	outUnit.worldOrigin = worldOrigin;
	outUnit.worldBounds = worldBounds;
	outUnit.bOnScreen = g_render->world_to_screen(worldOrigin, outUnit.screenOrigin, ctx.viewMatrix);
	outUnit.screenBoxVerts = screenVerts;
	return true;
}