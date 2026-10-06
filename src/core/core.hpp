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

        // ★2026-10-06：偏移校准异步化。指纹命中 → onReady 同步执行（SDK 就绪）；
        // 未命中 → 后台线程扫描（渲染/菜单照常响应，overlay 显示进度），完成后回调。
        const bool offsetsReady = update::kick( [ & ]( ) -> void {
            LOG( "Initializing SDK...\n" );
            if ( !sdk::init( ) )
                LOG( "Failed to initialize sdk.\n" );
            else
                LOG( "SDK initialized successfully!\n" );
            update::g_ready = true;
        } );

        if ( offsetsReady )
        {
            LOG( "Initializing SDK...\n" );
            if ( !sdk::init( ) )
            {
                LOG( "Failed to initialize sdk.\n" );
                return false;
            }
            LOG( "SDK initialized successfully!\n" );
            update::g_ready = true;
        }

        // 数据线程：等偏移校准就绪后再开始业务循环（校准期间零 DMA 竞争）
		std::thread( [ & ]( ) 
        {
            while ( !update::g_ready )
                std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );

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
