#pragma once

// Offsets are hardcoded for War Thunder 2.59.0.44
// Signature scanning and version detection have been removed.
namespace update
{
    inline auto run( ) -> bool
    {
        // All offsets are now hardcoded in offsets.hpp
        // No signature scanning or version detection needed.
        LOG( "Offsets loaded for War Thunder 2.59.0.44\n" );
        return true;
    }
}
