#ifdef _WIN32

#include "filesystem/io/windows/WinAwaiters.hpp"

#include "logging/log.hpp"
#include "trantor/net/EventLoop.h"

namespace idhan
{

// ─── ReadAwaiterWin ───────────────────────────────────────────────────────────

ReadAwaiterWin::ReadAwaiterWin( std::shared_ptr< std::vector< std::byte > > data ) :
  WinAwaiterBase( Type::READ ),
  m_data( std::move( data ) )
{}

void ReadAwaiterWin::complete( const DWORD bytes_transferred, const bool success )
{
	if ( !success )
	{
		const DWORD err { GetLastError() };
		log::error( "IOCP/IoRing read failed, error: {}", err );
		m_exception = std::make_exception_ptr(
			std::runtime_error( "Async read failed, Windows error: " + std::to_string( err ) ) );
	}
	else
	{
		m_data->resize( bytes_transferred );
	}

	if ( m_event_loop )
		m_event_loop->queueInLoop( m_cont );
	else
		m_cont.resume();
}

bool ReadAwaiterWin::await_ready() const noexcept
{
	return m_completed_sync;
}

void ReadAwaiterWin::await_suspend( const std::coroutine_handle<> h )
{
	m_event_loop = trantor::EventLoop::getEventLoopOfCurrentThread();
	m_cont = h;
}

std::vector< std::byte > ReadAwaiterWin::await_resume() const
{
	if ( m_exception ) std::rethrow_exception( m_exception );
	return *m_data;
}

// ─── WriteAwaiterWin ──────────────────────────────────────────────────────────

WriteAwaiterWin::WriteAwaiterWin() : WinAwaiterBase( Type::WRITE )
{}

void WriteAwaiterWin::complete( [[maybe_unused]] const DWORD bytes_transferred, const bool success )
{
	if ( !success )
	{
		const DWORD err { GetLastError() };
		log::error( "IOCP/IoRing write failed, error: {}", err );
		m_exception = std::make_exception_ptr(
			std::runtime_error( "Async write failed, Windows error: " + std::to_string( err ) ) );
	}

	if ( m_event_loop )
		m_event_loop->queueInLoop( m_cont );
	else
		m_cont.resume();
}

bool WriteAwaiterWin::await_ready() noexcept
{
	return false;
}

void WriteAwaiterWin::await_suspend( const std::coroutine_handle<> h )
{
	m_event_loop = trantor::EventLoop::getEventLoopOfCurrentThread();
	m_cont = h;
}

void WriteAwaiterWin::await_resume() const
{
	if ( m_exception ) std::rethrow_exception( m_exception );
}

} // namespace idhan

#endif // _WIN32
