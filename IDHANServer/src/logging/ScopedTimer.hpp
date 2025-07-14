//
// Created by kj16609 on 9/30/24.
//

#pragma once
#include <chrono>

#include "logging/log.hpp"

namespace idhan::logging
{

struct ScopedTimer
{
	std::string name;

	using Clock = std::chrono::high_resolution_clock;
	using TimePoint = std::chrono::time_point< Clock >;

	const TimePoint start;

	ScopedTimer( const std::string& str ) : name( str ), start( Clock::now() )
	{
		// Print start time
		// log::info( "{} started", name );
	}

	~ScopedTimer()
	{
		const auto end { Clock::now() };
		const auto duration_s { std::chrono::duration_cast< std::chrono::seconds >( end - start ).count() };
		const auto duration_ms { std::chrono::duration_cast< std::chrono::milliseconds >( end - start ).count() };

		//Print in seconds

		if ( duration_s > 1 || duration_ms > 500 ) log::info( "{} took {}s {}ms", name, duration_s, duration_ms % 1000 );
	}
};

} // namespace idhan::logging
