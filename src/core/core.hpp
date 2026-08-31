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

        LOG( "Initializing SDK...\n" );
        if ( !sdk::init( ) )
        {
            LOG( "Failed to initialize sdk.\n" );
            return false;
        }
        LOG( "SDK initialized successfully!\n" );

		std::thread( [ & ]( ) 
        {
        
            while ( true ) 
            {
                try
                {
                    misc::GameUpdate( );
                }
                catch ( ... )
                {
                    // prevent crash from killing the thread
                }

                std::this_thread::sleep_for( std::chrono::milliseconds( 1 ) );
            }

        }).detach( );
	    
        return true;
	}

}
