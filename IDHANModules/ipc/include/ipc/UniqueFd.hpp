#pragma once

#include <utility>

#ifdef __linux__
#include <unistd.h>
#endif

namespace idhan::ipc
{

//! Owning wrapper around a file descriptor.
class UniqueFd
{
	int m_fd { -1 };

  public:

	UniqueFd() = default;

	explicit UniqueFd( const int fd ) : m_fd( fd ) {}

	~UniqueFd() { reset(); }

	UniqueFd( const UniqueFd& ) = delete;
	UniqueFd& operator=( const UniqueFd& ) = delete;

	UniqueFd( UniqueFd&& other ) noexcept : m_fd( std::exchange( other.m_fd, -1 ) ) {}

	UniqueFd& operator=( UniqueFd&& other ) noexcept
	{
		if ( this != &other ) reset( std::exchange( other.m_fd, -1 ) );
		return *this;
	}

	[[nodiscard]] int get() const { return m_fd; }

	//! Relinquishes ownership; the caller is responsible for closing the returned descriptor.
	[[nodiscard]] int release() { return std::exchange( m_fd, -1 ); }

	//! Closes the held descriptor (if any) and takes ownership of \p fd.
	void reset( const int fd = -1 )
	{
		// Guard against self-reset closing the descriptor we are about to store.
		if ( fd == m_fd )
		{
			m_fd = fd;
			return;
		}
#ifdef __linux__
		if ( m_fd >= 0 ) ::close( m_fd );
#endif
		m_fd = fd;
	}

	[[nodiscard]] explicit operator bool() const { return m_fd >= 0; }
};

} // namespace idhan::ipc
