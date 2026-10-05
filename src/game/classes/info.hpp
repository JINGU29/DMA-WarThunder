#pragma once

class c_info {
public:
	c_info( const std::uintptr_t base_address = 0 ) : base_address( base_address ) { }

public:

	auto getVehicleName( ) -> std::string {
		uintptr_t addr = TargetProcess->Read< uintptr_t >( this->base_address + 0x28 );
		return TargetProcess->ReadString( addr );
	}

	auto getUnitType( ) -> std::string {
		uintptr_t addr = TargetProcess->Read< uintptr_t >( this->base_address + 0x8C );
		return TargetProcess->ReadString( addr );
	}

	// 2.59：unit+0x8C 是 u8 类型枚举（0=空中/直升机 3=地面 5=其他），不是字符串指针。
	// 旧实现把该字节当 char* 解引用 → ReadString 永远失败 → isPlane() 恒 false（2.59 实测）。
	bool isPlane( ) {
		return TargetProcess->Read< uint8_t >( this->base_address + 0x8C ) == 0;
	}


private:
	uintptr_t base_address;
};

