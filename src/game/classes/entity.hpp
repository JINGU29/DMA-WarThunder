#pragma once

class c_entity {
public:
	auto init( ) -> bool {
		this->base_address = TargetProcess->Read< uintptr_t >( baseAddr + offsets::globals::local_player );
		return this->base_address != 0;
	}

public:

    auto getGuiState( ) -> uint8_t {
		return TargetProcess->Read< uint8_t >( this->base_address + offsets::localplayer::guiState_offset );
	}

	auto getLocalUnit( ) -> c_unit {
		uintptr_t addr = TargetProcess->Read< uintptr_t >( baseAddr + offsets::globals::my_unit );
		return c_unit( addr );
	}

private:
    uintptr_t base_address;
};
