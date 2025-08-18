//
// Created by kj16609 on 7/16/25.
//

#include "ScopedTimer.hpp"

namespace idhan::logging
{

ScopedTimer::ScopedTimer( const std::string& str, const bool warn, const std::chrono::milliseconds ms_warn_time ) :
  name( str ),
  m_warn_time( ms_warn_time ),
  m_always_warn( warn ),
  start( Clock::now() )
{}

ScopedTimer::~ScopedTimer()
{
	const auto end { Clock::now() };
	const auto duration { end - start };
	const auto duration_s { std::chrono::duration_cast< std::chrono::seconds >( duration ).count() };
	const auto duration_ms { std::chrono::duration_cast< std::chrono::milliseconds >( duration ).count() };

	if ( duration > m_warn_time || m_always_warn )
		log::warn( "{} took {}s {}ms", name, duration_s, duration_ms % 1000 );
}
} // namespace idhan::logging
