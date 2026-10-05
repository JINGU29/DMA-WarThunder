#pragma once

#include "..\features\misc\misc.hpp"

namespace GUI
{

	inline auto OnRender( ) -> void
	{
	
		ImGui::SetNextWindowSize( ImVec2( 350, 350 ), ImGuiCond_Once );
		ImGui::Begin( "SDK - https://github.com/MHSPlay", nullptr );
		{
			
			ImGui::Text( "Hello World!" );

			ImGui::Separator( );

			// 弹道预测（提前量 + 下坠补偿，坦克炮弹用）
			ImGui::Checkbox( "Ballistic Prediction", &misc::bBallisticPrediction );

			// 弹道落点部位选择（DamageModel 部件盒驱动；部件数据未加载时回落车体中心）
			const char* aimParts[] = { "Body Center (default)", "Breech", "Ammo Rack", "Crew" };
			ImGui::Combo( "Aim Part", &misc::ballisticAimPart, aimParts, 4 );
			ImGui::TextColored( ImVec4( 0.55f, 0.95f, 1.0f, 1.0f ), "Cyan box = selected part" );

			ImGui::TextColored( ImVec4( 0.6f, 0.6f, 0.8f, 1.0f ), "Dashed line from tank to predicted hit (red box)" );

			ImGui::Separator( );

			// DamageModel 部件标记（Style 1：真实盒线框 + 远距兜底框；分级 LOD 防糊）——四独立开关：乘员/弹药/油箱/炮闩
			ImGui::Checkbox( "Part Markers: Crew", &misc::bPartMarkersCrew );
			ImGui::Checkbox( "Part Markers: Ammo", &misc::bPartMarkersAmmo );
			ImGui::Checkbox( "Part Markers: Fuel Tank", &misc::bPartMarkersFuel );
			ImGui::Checkbox( "Part Markers: Breech", &misc::bPartMarkersBreech );
			ImGui::TextColored( ImVec4( 1.0f, 0.55f, 0.1f, 1.0f ), "Orange = Crew" );
			ImGui::SameLine( );
			ImGui::TextColored( ImVec4( 1.0f, 0.18f, 0.18f, 1.0f ), "Red = Ammo" );
			ImGui::TextColored( ImVec4( 0.31f, 0.86f, 0.35f, 1.0f ), "Green = Fuel" );
			ImGui::SameLine( );
			ImGui::TextColored( ImVec4( 1.0f, 0.86f, 0.24f, 1.0f ), "Yellow = Breech" );

		}
		ImGui::End( );
	}

}