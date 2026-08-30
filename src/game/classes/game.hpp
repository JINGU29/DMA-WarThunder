#pragma once

class c_game {
public:
	auto init( ) -> bool {
		this->base_address = TargetProcess->Read< uintptr_t >( baseAddr + offsets::cgame_offset );
		return this->base_address != 0;
	}

	// 重新初始化：在非存活/非观战状态时调用
	auto reinit( ) -> bool {
		if ( !init( ) )
			return false;
		if ( !ballistics->init( this->base_address ) )
			return false;
		if ( !camera->init( this->base_address ) )
			return false;
		return true;
	}

	auto get_base( ) -> uintptr_t {
		return this->base_address;
	}

public:

	auto getUnitList( ) -> uintptr_t {
		return TargetProcess->Read< uintptr_t >( this->base_address + 0x0340 );
	}

	auto getUnitCount( ) -> int {
		return TargetProcess->Read< int >( this->base_address + 0x0350 );
	}

	c_ballistic* ballistics = new c_ballistic;
	c_camera* camera = new c_camera;

private:
	uintptr_t base_address;
};
