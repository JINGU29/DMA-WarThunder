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
			ImGui::TextColored( ImVec4( 0.6f, 0.6f, 0.8f, 1.0f ), "Dashed line from tank to predicted hit (red box)" );

		}
		ImGui::End( );
	}

}