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
	const std::chrono::high_resolution_clock::time_point start;

	ScopedTimer( const std::string& str ) : name( str ), start( std::chrono::high_resolution_clock::now() )
	{
		// Print start time
		log::info( "{} started", name );
	}

	~ScopedTimer()
	{
		const auto end { std::chrono::high_resolution_clock::now() };
		const auto duration { std::chrono::duration_cast< std::chrono::milliseconds >( end - start ).count() };

		//Print in seconds
		const auto seconds { duration / 1000.0 };

		log::info( "{} took {} seconds", name, seconds );
	}
};

} // namespace idhan::logging
