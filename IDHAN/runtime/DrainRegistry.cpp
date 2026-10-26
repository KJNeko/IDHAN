#include "idhan/runtime/DrainRegistry.hpp"

#include <folly/coro/Baton.h>

#include <mutex>
#include <stdexcept>
#include <utility>

namespace idhan::runtime
{
struct WorkRegistration::State
{
	std::mutex mutex;
	std::size_t count { 0 };
	bool closed { false };
	folly::coro::Baton drained;

	void release() noexcept
	{
		bool finished;
		{
			std::lock_guard lock( mutex );
			--count;
			finished = closed && count == 0;
		}
		// Posting may resume a coroutine inline; never do so while holding the lock.
		if ( finished ) drained.post();
	}

	void close()
	{
		bool finished;
		{
			std::lock_guard lock( mutex );
			closed = true;
			finished = count == 0;
		}
		if ( finished ) drained.post();
	}

	static Task<> wait( std::shared_ptr< State > state ) { co_await state->drained; }
};

WorkRegistration::WorkRegistration( std::shared_ptr< State > state ) : m_state( std::move( state ) )
{}

WorkRegistration::WorkRegistration( WorkRegistration&& ) noexcept = default;

WorkRegistration& WorkRegistration::operator=( WorkRegistration&& other ) noexcept
{
	if ( this == &other ) return *this;
	if ( m_state ) m_state->release();
	m_state = std::move( other.m_state );
	return *this;
}

WorkRegistration::~WorkRegistration()
{
	if ( m_state ) m_state->release();
}

WorkRegistration WorkRegistration::child() const
{
	if ( !m_state ) throw std::logic_error( "Cannot register a child of moved-from work" );
	std::lock_guard lock( m_state->mutex );
	++m_state->count;
	return WorkRegistration( m_state );
}

DrainRegistry::DrainRegistry() : m_state( std::make_shared< WorkRegistration::State >() )
{}

DrainRegistry::~DrainRegistry() = default;

std::optional< WorkRegistration > DrainRegistry::admit()
{
	std::lock_guard lock( m_state->mutex );
	if ( m_state->closed ) return std::nullopt;
	++m_state->count;
	return WorkRegistration( m_state );
}

void DrainRegistry::closeAdmission()
{
	m_state->close();
}

std::size_t DrainRegistry::outstanding() const
{
	std::lock_guard lock( m_state->mutex );
	return m_state->count;
}

Task<> DrainRegistry::drain()
{
	auto state = m_state;
	state->close();
	return WorkRegistration::State::wait( std::move( state ) );
}
} // namespace idhan::runtime
