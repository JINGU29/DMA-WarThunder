#pragma once

// Offsets are hardcoded for War Thunder 2.57.1.115
// Signature scanning and version detection have been removed.
namespace update
{
    inline auto run( ) -> bool
    {
        // All offsets are now hardcoded in offsets.hpp
        // No signature scanning or version detection needed.
        LOG( "Offsets loaded for War Thunder 2.57.1.115\n" );
        return true;
    }
}
