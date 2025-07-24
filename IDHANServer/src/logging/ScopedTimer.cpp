//
// Created by kj16609 on 7/16/25.
//

#include "ScopedTimer.hpp"

namespace idhan::logging
{

ScopedTimer::ScopedTimer( const std::string& str ) : name( str ), start( Clock::now() )
{
	// Print start time
	// log::info( "{} started", name );
}

ScopedTimer::~ScopedTimer()
{
	const auto end { Clock::now() };
	const auto duration_s { std::chrono::duration_cast< std::chrono::seconds >( end - start ).count() };
	const auto duration_ms { std::chrono::duration_cast< std::chrono::milliseconds >( end - start ).count() };

	if ( duration_s > 15 ) log::warn( "{} took {}s {}ms", name, duration_s, duration_ms % 1000 );
}
} // namespace idhan::logging
