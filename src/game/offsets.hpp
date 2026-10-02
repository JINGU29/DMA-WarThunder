#pragma once

#include <cstdint>

// War Thunder 2.59.0.44 offsets - dumped from WT/OPS (monkrel.cc)
namespace offsets
{
	// Global offsets (from module base)
	namespace globals
	{
		// g_GameContext -> c_game pointer (was cgame_offset)
		constexpr uintptr_t game_context = 0x7A23268;
		// g_LocalPlayer -> local player entity (was localplayer_offset)
		constexpr uintptr_t local_player = 0x79FA898;
		// g_MyUnit -> local player's current unit
		constexpr uintptr_t my_unit = 0x7A25210;
		// g_ViewAngles
		constexpr uintptr_t view_angles = 0x7A27D78;
		// g_ViewMatrix
		constexpr uintptr_t view_matrix = 0x7A7D2F8;
		// g_IsScoping
		constexpr uintptr_t is_scoping = 0x7A6C47C;
		// g_ScreenWidth
		constexpr uintptr_t screen_width = 0x7E695D0;
		// g_HudInfo
		constexpr uintptr_t hud_info = 0x7A22890;
		// g_GameOptics
		constexpr uintptr_t game_optics = 0x7A250E0;
		// g_AirPredictionBool
		constexpr uintptr_t air_prediction_bool = 0x7E8BDA0;
		// g_AllListData (not in 2.59.0.44 API dump, keeping old value)
		constexpr uintptr_t all_list_data = 0x74CF748;
		// g_BombListIndexPtr
		constexpr uintptr_t bomb_list_index_ptr = 0x79FDF10;
		// g_RocketListIndexPtr
		constexpr uintptr_t rocket_list_index_ptr = 0x79FF450;
		// g_UIPosArr
		constexpr uintptr_t ui_pos_arr = 0x7A6F6A8;
	}

	namespace cgame_offsets
	{
		constexpr uintptr_t ballistics_offset = 0x3F0;
		constexpr uintptr_t camera_offset = 0x660;
		constexpr uintptr_t current_map = 0x1F0;

		// Unit lists (group 3 - active units)
		constexpr uintptr_t unit_list_1 = 0x310;
		constexpr uintptr_t unit_list_2 = 0x328;
		constexpr uintptr_t unit_list_3 = 0x340;

		constexpr uintptr_t unit_count_1 = 0x320;
		constexpr uintptr_t unit_count_2 = 0x338;
		constexpr uintptr_t unit_count_3 = 0x350;

		namespace camera_offsets
		{
			constexpr uintptr_t camera_matrix_offset = 0x1D8;
			constexpr uintptr_t camera_position_offset = 0x60;
		}

		namespace ballistic_offsets
		{
			constexpr uintptr_t bomb_impact_point = 0x1CCC;
			// Bullet impact point - not in new dump, keeping old value
			constexpr uintptr_t bullet_impact_point = 0x22C8 + 0x20;
			// Ballistics data (velocity, mass, caliber, length, max_dist) - not in new dump, keeping old values
			constexpr uintptr_t velocity = 0x2050;
			constexpr uintptr_t mass = 0x205C;
			constexpr uintptr_t caliber = 0x2060;
			constexpr uintptr_t length = 0x2048;
			constexpr uintptr_t max_dist = 0x2068;
			constexpr uintptr_t selected_unit_ptr = 0x6B0;
			constexpr uintptr_t ballistics_ptr = 0x3f0;
			constexpr uintptr_t ingame_ballistics = 0x2460;
			constexpr uintptr_t weapon_position = 0x1F00;

			constexpr uintptr_t telecontrol_offset = 0xcb8;
			namespace telecontrol_offsets
			{
				constexpr uintptr_t gameui_offset = 0x928;
				namespace gameui_offsets
				{
					constexpr uintptr_t mouse_pos = 0x810;
				}
			}
		}
	}

	namespace localplayer
	{
		constexpr uintptr_t guiState_offset = 0x640;
		constexpr uintptr_t controlledUnit_offset = 0x878;
		constexpr uintptr_t ownedUnit_offset = 0x848;
		constexpr uintptr_t spectatedModelIndex_offset = 0x670;
		constexpr uintptr_t name_offset = 0x60;
		// Flags for isLocal check: (player+0x90 >> 9) & 1
		constexpr uintptr_t publicFlags_offset = 0x90;
	}

	namespace unit_offsets
	{
		constexpr uintptr_t ground_velocity_offset = 0x5C;
		constexpr uintptr_t airmovement_offset = 0x10;
		constexpr uintptr_t position_offset = 0xD40;
		constexpr uintptr_t rotation_matrix_offset = 0xD1C;
		constexpr uintptr_t bbmax_offset = 0x274;
		constexpr uintptr_t bbmin_offset = 0x268;
		constexpr uintptr_t body_bbmax_offset = 0x218C;
		constexpr uintptr_t body_bbmin_offset = 0x2180;
		constexpr uintptr_t unitState_offset = 0xF98;
		constexpr uintptr_t teamNum_offset = 0x1018;
		constexpr uintptr_t unitType_offset = 0x8C;
		constexpr uintptr_t unitIndex_offset = 0x8;
		constexpr uintptr_t unitFlags1_offset = 0x90;
		constexpr uintptr_t unitFlags2_offset = 0x91;
		constexpr uintptr_t unitFlags3_offset = 0x92;
		constexpr uintptr_t unitFlags4_offset = 0x93;
		constexpr uintptr_t visualReload_offset = 0xAF0;
		constexpr uintptr_t info_offset = 0x1028;
		constexpr uintptr_t invulnerable_offset = 0xE98;
		constexpr uintptr_t invulTimer_offset = 0xE74;
		constexpr uintptr_t velocity_offset = 0x2100;
		constexpr uintptr_t airContainer_offset = 0xD50;
		constexpr uintptr_t armory_offset = 0x10D0;
		constexpr uintptr_t damageModelCont_offset = 0x10A8;
		constexpr uintptr_t playerInfo_offset = 0xFA0;
		constexpr uintptr_t groundmovement_offset = 0x1F00;
	}

	namespace unit_info_offsets
	{
		constexpr uintptr_t bomberView_offset = 0x454;
		constexpr uintptr_t haveCCIPForBombs_offset = 0x459;
		constexpr uintptr_t haveCCIPForGun_offset = 0x457;
		constexpr uintptr_t haveCCIPForRocket_offset = 0x456;
		constexpr uintptr_t haveCCIPForTurret_offset = 0x458;
	}
}
