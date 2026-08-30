#pragma once

namespace core 
{

	inline auto Thread( ) -> bool
	{

        if ( !TargetProcess->Init( "aces.exe" ) )
        {
            LOG( "Failed to initialize process.\n" );
            return false;
        }

        baseAddr = TargetProcess->GetBaseAddress( "aces.exe" );
        baseSize = TargetProcess->GetBaseSize( "aces.exe" );

        if ( !update::run( ) )
        {
            LOG( "Failed to update sdk.\n" );
            return false;
        }

        update::parse_offsets( );

        if ( !sdk::init( ) )
        {
            LOG( "Failed to initialize sdk.\n" );
            return false;
        }

		std::thread( [ & ]( ) 
        {
        
            while ( true ) 
            {
				// GameUpdate 线程：预计算所有渲染数据
				// 内部包含缓存检查、scatter read、预计算、mutex 写入
                misc::GameUpdate( );

                std::this_thread::sleep_for( std::chrono::milliseconds( 100 ) );
            }

        }).detach( );
	    
        return true;
	}

}
