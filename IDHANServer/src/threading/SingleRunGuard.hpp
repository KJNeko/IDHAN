#pragma once

#include <atomic>

namespace idhan
{

// Owns a job's gate across coroutine suspension and releases it on every exit path.
class SingleRunGuard
{
	std::atomic< bool >& m_running;
	const bool m_acquired;

  public:

	explicit SingleRunGuard( std::atomic< bool >& running ) :
	  m_running( running ),
	  m_acquired( !running.exchange( true ) )
	{}

	SingleRunGuard( const SingleRunGuard& ) = delete;
	SingleRunGuard& operator=( const SingleRunGuard& ) = delete;

	~SingleRunGuard()
	{
		if ( m_acquired ) m_running.store( false );
	}

	[[nodiscard]] bool acquired() const { return m_acquired; }
};

} // namespace idhan
