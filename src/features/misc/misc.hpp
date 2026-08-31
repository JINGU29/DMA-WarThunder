#pragma once

namespace misc
{
	inline std::vector< c_unit > unitsList;

	inline auto is_valid_enemy( c_unit unit ) -> bool
	{
		// https://www.unknowncheats.me/forum/4631229-post2807.html
		if ( unit.getUnitState( ) >= 2 ) 
			return false;

		const uint8_t unit_team = unit.getTeam( );
		if ( unit_team == 0 || unit_team == sdk::cLocalPlayer->getLocalUnit( ).getTeam( ) )
			return false;
		
		return true;
	}

	inline auto UpdateEntityList( ) -> void 
	{
		std::vector< c_unit > temp_units;

		const auto gui_state = sdk::cLocalPlayer->getGuiState( );

		// debug log for all states (throttled to 5s)
		static auto last_debug_log = std::chrono::steady_clock::now( );
		auto now = std::chrono::steady_clock::now( );
		static uint8_t last_state = 0xFF;
		bool state_changed = ( gui_state != last_state );
		last_state = gui_state;

		if ( gui_state != GuiState::ALIVE && gui_state != GuiState::SPECTATE && gui_state != GuiState::SPEC && gui_state != GuiState::BATTLE )
		{
			if ( state_changed || std::chrono::duration_cast< std::chrono::seconds >( now - last_debug_log ).count( ) >= 5 )
			{
				const char* state_str = "UNKNOWN";
				switch ( gui_state )
				{
					case GuiState::NONE:        state_str = "NONE(lobby)"; break;
					case GuiState::MENU:        state_str = "MENU"; break;
					case GuiState::LOADING:     state_str = "LOADING"; break;
					case GuiState::SPAWN_MENU:  state_str = "SPAWN_MENU"; break;
					case GuiState::DEAD:        state_str = "DEAD"; break;
					case GuiState::SPECTATE:    state_str = "SPECTATE"; break;
					case GuiState::BATTLE:      state_str = "BATTLE"; break;
					default: break;
				}
				LOG( "Waiting for battle... state: %s (gui_state=%d)\n", state_str, gui_state );
				last_debug_log = now;
			}

			sdk::cLocalPlayer->init( );
			unitsList.clear( );
			return;
		}

		// entered battle - log once on state change
		if ( state_changed )
		{
			LOG( "Entered battle! (gui_state=%d)\n", gui_state );
			last_debug_log = now;
		}

		const int unit_count = sdk::cGame->getUnitCount( );
		if ( !unit_count || unit_count > 10000 ) {
			unitsList.clear( );
			return;
		}

		const uintptr_t unit_list_base = sdk::cGame->getUnitList( );
		if ( !unit_list_base )
			return;

		VMMDLL_SCATTER_HANDLE hScatter = TargetProcess->CreateScatterHandle( );
		if ( !hScatter )
			return;

		auto scatter_unit = [ & ]( VMMDLL_SCATTER_HANDLE handle, uint32_t count ) -> std::vector< c_unit >
		{
			std::vector< std::uintptr_t > pointers( count );
			std::vector< c_unit > result;
			result.reserve( count );

			for (size_t i = 0; i < count; i++)
				TargetProcess->AddScatterReadRequest( handle, unit_list_base + 0x8 * i, &pointers[ i ], sizeof( std::uintptr_t ) );
			
			TargetProcess->ExecuteReadScatter( handle );

			for ( size_t i = 0; i < count; i++ ) {
				if ( pointers.at( i ) )
					result.emplace_back( c_unit( pointers.at( i ) ) );
			}

			return result;
		};

		std::vector< c_unit > units = scatter_unit( hScatter, unit_count );
		for ( c_unit unit : units ) 
		{
			if ( unit.get_base( ) && is_valid_enemy( unit ) )
				temp_units.emplace_back( unit );

		}

		TargetProcess->CloseScatterHandle( hScatter );
		unitsList = temp_units;
		temp_units.clear( );

	}

}
