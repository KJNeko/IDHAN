//
// Created by kj16609 on 7/16/25.
//

#include "ScopedTimer.hpp"

#include "Config.hpp"

namespace idhan::logging
{

ScopedTimer::ScopedTimer( const std::string& str, const std::chrono::microseconds ms_warn_time ) :
  name( str ),
  m_warn_time( ms_warn_time ),
  start( Clock::now() )
{}

ScopedTimer::~ScopedTimer()
{
	const auto end { Clock::now() };
	const auto duration { end - start };
	const auto duration_s { std::chrono::duration_cast< std::chrono::seconds >( duration ).count() };
	const auto duration_ms { std::chrono::duration_cast< std::chrono::milliseconds >( duration ).count() };
	const auto duration_us { std::chrono::duration_cast< std::chrono::microseconds >( duration ).count() };

	if ( config::get< bool >( "logging", "enable_perf_warnings", false ) && duration > m_warn_time )
		log::warn( "{} took {}s {}ms {}us", name, duration_s, duration_ms % 1000, duration_us % ( 1000 ) );
}

} // namespace idhan::logging
