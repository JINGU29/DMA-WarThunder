#pragma once

namespace esp 
{
    inline std::array< vec3_t, 8 > calculate_bbox_corners( const vec3_t& position, const vec3_t& bbmin, const vec3_t& bbmax, const matrix3x4_t& rot )
    {
        const auto r = rot.right;
        const auto f = rot.forward;
        const auto u = rot.up;

        // only 18 multipl
        const vec3_t rx0 = { r.x * bbmin.x, r.y * bbmin.x, r.z * bbmin.x };
        const vec3_t rx1 = { r.x * bbmax.x, r.y * bbmax.x, r.z * bbmax.x };
        const vec3_t fy0 = { f.x * bbmin.y, f.y * bbmin.y, f.z * bbmin.y };
        const vec3_t fy1 = { f.x * bbmax.y, f.y * bbmax.y, f.z * bbmax.y };
        const vec3_t uz0 = { u.x * bbmin.z, u.y * bbmin.z, u.z * bbmin.z };
        const vec3_t uz1 = { u.x * bbmax.z, u.y * bbmax.z, u.z * bbmax.z };

        const auto make = [ & ]( const vec3_t& rx, const vec3_t& fy, const vec3_t& uz ) -> vec3_t {
            return {
                position.x + rx.x + fy.x + uz.x,
                position.y + rx.y + fy.y + uz.y,
                position.z + rx.z + fy.z + uz.z
            };
        };

        return { {
            make( rx0, fy0, uz0 ),
            make( rx1, fy0, uz0 ),
            make( rx0, fy1, uz0 ),
            make( rx1, fy1, uz0 ),
            make( rx0, fy0, uz1 ),
            make( rx1, fy0, uz1 ),
            make( rx0, fy1, uz1 ),
            make( rx1, fy1, uz1 ),
        } };
    }


    inline void draw_wireframe_box( const std::array< vec2_t, 8 >& corners, ImU32 color, float thickness ) {

        g_render->line( corners[ 0 ].x, corners[ 0 ].y, corners[ 1 ].x, corners[ 1 ].y, color, thickness );
        g_render->line( corners[ 1 ].x, corners[ 1 ].y, corners[ 3 ].x, corners[ 3 ].y, color, thickness );
        g_render->line( corners[ 3 ].x, corners[ 3 ].y, corners[ 2 ].x, corners[ 2 ].y, color, thickness );
        g_render->line( corners[ 2 ].x, corners[ 2 ].y, corners[ 0 ].x, corners[ 0 ].y, color, thickness );

        g_render->line( corners[ 4 ].x, corners[ 4 ].y, corners[ 5 ].x, corners[ 5 ].y, color, thickness );
        g_render->line( corners[ 5 ].x, corners[ 5 ].y, corners[ 7 ].x, corners[ 7 ].y, color, thickness );
        g_render->line( corners[ 7 ].x, corners[ 7 ].y, corners[ 6 ].x, corners[ 6 ].y, color, thickness );
        g_render->line( corners[ 6 ].x, corners[ 6 ].y, corners[ 4 ].x, corners[ 4 ].y, color, thickness );

        g_render->line( corners[ 0 ].x, corners[ 0 ].y, corners[ 4 ].x, corners[ 4 ].y, color, thickness );
        g_render->line( corners[ 1 ].x, corners[ 1 ].y, corners[ 5 ].x, corners[ 5 ].y, color, thickness );
        g_render->line( corners[ 2 ].x, corners[ 2 ].y, corners[ 6 ].x, corners[ 6 ].y, color, thickness );
        g_render->line( corners[ 3 ].x, corners[ 3 ].y, corners[ 7 ].x, corners[ 7 ].y, color, thickness );
    }

    inline void draw_crosshair( ) {
        static float centerX = sdk::screen_width / 2.0f;
        static float centerY = sdk::screen_height / 2.0f;

        ImU32 color = IM_COL32( 0, 255, 0, 255 );

        int size = 10;
        int gap = 1;
        float thickness = 1.0f;
        float outlineThickness = 3.0f;

        g_render->line( centerX - size, centerY, centerX - gap, centerY, color, thickness );
        g_render->line( centerX + gap, centerY, centerX + size, centerY, color, thickness );
        g_render->line( centerX, centerY - size, centerX, centerY - gap, color, thickness );
        g_render->line( centerX, centerY + gap, centerX, centerY + size, color, thickness );
    }

    inline auto run( ) -> void 
	{
        const auto camera_matrix = sdk::cGame->camera->getCameraMatrix( );

        draw_crosshair( );

        const auto gui_state = sdk::cLocalPlayer->getGuiState( );
        if ( gui_state != GuiState::ALIVE && gui_state != GuiState::SPECTATE && gui_state != GuiState::SPEC && gui_state != GuiState::BATTLE )
			return;

        if ( sdk::cLocalPlayer->getLocalUnit( ).getInfo( ).isPlane( ) ) 
        {
            vec3_t bombImpact = sdk::cGame->ballistics->getBombImpactPoint( );

            vec2_t screen_position;
            if ( g_render->world_to_screen( bombImpact, screen_position, camera_matrix ) )
                g_render->circle( screen_position.x, screen_position.y, 6.0f, IM_COL32( 255, 0, 200, 255 ), 16.0f );
            
        }

        // iter
        std::vector< c_unit > units = misc::unitsList;
		for ( c_unit& unit : units )
		{
            vec3_t unit_position = unit.getPosition( );
			if ( unit_position.empty( ) )
				continue;

            vec2_t screen_position;
			if ( !g_render->world_to_screen( unit_position, screen_position, camera_matrix ) )
				continue;

            const vec3_t local_position = sdk::cLocalPlayer->getLocalUnit( ).getPosition( );
            const float distance = local_position.dist_to( unit_position );
            if ( distance >= 2000 )
                continue;

            const vec3_t bbmin = unit.getBBMin( );
            const vec3_t bbmax = unit.getBBMax( );
            const matrix3x4_t rotation = unit.getMatrixRotation( );
            const auto world_corners = calculate_bbox_corners( unit_position, bbmin, bbmax, rotation );

            std::array< vec2_t, 8 > screen_corners;
            bool corners_visible[ 8 ] = {};
            int visible_count = 0;

            for ( size_t i = 0; i < world_corners.size( ); ++i ) 
            {
                if ( g_render->world_to_screen( world_corners[ i ], screen_corners[ i ], camera_matrix ) )
                {
                    corners_visible[ i ] = true;
                    ++visible_count;
                }
            }

            // 只要至少1个顶点可见就画框
            if ( visible_count > 0 ) 
            {
                // 用第一个可见顶点初始化边界
                float box_bottom_y = 0.0f;
                float box_top_y = 0.0f;
                float box_right_x = 0.0f;
                bool first = true;

                for ( size_t i = 0; i < screen_corners.size( ); ++i )
                {
                    if ( !corners_visible[ i ] )
                        continue;

                    if ( first )
                    {
                        box_bottom_y = screen_corners[ i ].y;
                        box_top_y    = screen_corners[ i ].y;
                        box_right_x  = screen_corners[ i ].x;
                        first = false;
                    }
                    else
                    {
                        box_bottom_y = max( box_bottom_y, screen_corners[ i ].y );
                        box_top_y    = min( box_top_y,    screen_corners[ i ].y );
                        box_right_x  = max( box_right_x,  screen_corners[ i ].x );
                    }
                }

                // 只画可见顶点之间的线框边
                // 底面四条边
                if ( corners_visible[0] && corners_visible[1] ) g_render->line( screen_corners[0].x, screen_corners[0].y, screen_corners[1].x, screen_corners[1].y, IM_COL32(255,0,0,255), 1.0f );
                if ( corners_visible[1] && corners_visible[3] ) g_render->line( screen_corners[1].x, screen_corners[1].y, screen_corners[3].x, screen_corners[3].y, IM_COL32(255,0,0,255), 1.0f );
                if ( corners_visible[3] && corners_visible[2] ) g_render->line( screen_corners[3].x, screen_corners[3].y, screen_corners[2].x, screen_corners[2].y, IM_COL32(255,0,0,255), 1.0f );
                if ( corners_visible[2] && corners_visible[0] ) g_render->line( screen_corners[2].x, screen_corners[2].y, screen_corners[0].x, screen_corners[0].y, IM_COL32(255,0,0,255), 1.0f );
                // 顶面四条边
                if ( corners_visible[4] && corners_visible[5] ) g_render->line( screen_corners[4].x, screen_corners[4].y, screen_corners[5].x, screen_corners[5].y, IM_COL32(255,0,0,255), 1.0f );
                if ( corners_visible[5] && corners_visible[7] ) g_render->line( screen_corners[5].x, screen_corners[5].y, screen_corners[7].x, screen_corners[7].y, IM_COL32(255,0,0,255), 1.0f );
                if ( corners_visible[7] && corners_visible[6] ) g_render->line( screen_corners[7].x, screen_corners[7].y, screen_corners[6].x, screen_corners[6].y, IM_COL32(255,0,0,255), 1.0f );
                if ( corners_visible[6] && corners_visible[4] ) g_render->line( screen_corners[6].x, screen_corners[6].y, screen_corners[4].x, screen_corners[4].y, IM_COL32(255,0,0,255), 1.0f );
                // 四条竖边
                if ( corners_visible[0] && corners_visible[4] ) g_render->line( screen_corners[0].x, screen_corners[0].y, screen_corners[4].x, screen_corners[4].y, IM_COL32(255,0,0,255), 1.0f );
                if ( corners_visible[1] && corners_visible[5] ) g_render->line( screen_corners[1].x, screen_corners[1].y, screen_corners[5].x, screen_corners[5].y, IM_COL32(255,0,0,255), 1.0f );
                if ( corners_visible[2] && corners_visible[6] ) g_render->line( screen_corners[2].x, screen_corners[2].y, screen_corners[6].x, screen_corners[6].y, IM_COL32(255,0,0,255), 1.0f );
                if ( corners_visible[3] && corners_visible[7] ) g_render->line( screen_corners[3].x, screen_corners[3].y, screen_corners[7].x, screen_corners[7].y, IM_COL32(255,0,0,255), 1.0f );

                // Distance + reload time display
                char distance_text[ 16 ];
                snprintf( distance_text, sizeof( distance_text ), "%dm", static_cast<int>(distance) );

                const vec2_t text_position = {
                    screen_position.x,
                    box_bottom_y + 5.0f
                };

                g_render->text( text_position, IM_COL32( 255, 255, 255, 255 ), 0, distance_text, g_render->fonts( ).m_esp );

                uint8_t reload_time = unit.getReloadTime( );
                char reload_text[ 16 ];
                constexpr float stat = ( 10.f / 16 );
                float progress = stat * reload_time * 0.1f;

                snprintf( reload_text, sizeof( reload_text ), "%.1fs", progress );

                const vec2_t reload_pos = {
                    screen_position.x,
                    box_bottom_y + 20.0f
                };

                g_render->text( reload_pos, IM_COL32( 0, 200, 255, 255 ), 0, reload_text, g_render->fonts( ).m_esp );
            }

            //aimbot::run( unit, unit_position, local_position, camera_matrix );

		}
	}
}