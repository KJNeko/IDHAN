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
	std::chrono::milliseconds m_warn_time;

	using Clock = std::chrono::high_resolution_clock;
	using TimePoint = std::chrono::time_point< Clock >;

	const TimePoint start;

	ScopedTimer( const std::string& str, std::chrono::milliseconds ms_warn_time = std::chrono::minutes( 1 ) );

	~ScopedTimer();
};

} // namespace idhan::logging
