//
// Created by kj16609 on 7/29/25.
//
#ifdef _WIN32

#include "filesystem/io/windows/IOUringW10.hpp"

#include "filesystem/io/windows/WinAwaiters.hpp"
#include "logging/log.hpp"
#include "trantor/net/EventLoop.h"

namespace idhan
{

void iocpWatcherThread(
	const std::stop_token& token,
	IOUringW10* backend,
	std::shared_ptr< std::atomic< bool > > running )
{
	log::info( "IOUringW10 (IOCP) watcher thread started" );

	if ( running->load() == false ) running->wait( false );

	static constexpr ULONG max_entries { 64 };
	OVERLAPPED_ENTRY entries[ max_entries ];

	while ( !token.stop_requested() )
	{
		ULONG count { 0 };

		// Post a dummy entry on stop to unblock GetQueuedCompletionStatusEx
		const BOOL ok {
			GetQueuedCompletionStatusEx( backend->m_iocp, entries, max_entries, &count, 500 /*ms*/, FALSE )
		};

		if ( !ok )
		{
			const DWORD err { GetLastError() };
			if ( err == WAIT_TIMEOUT ) continue;
			log::error( "GetQueuedCompletionStatusEx failed, error: {}", err );
			continue;
		}

		for ( ULONG i { 0 }; i < count; ++i )
		{
			OVERLAPPED* ovl { entries[ i ].lpOverlapped };
			if ( !ovl ) continue; // sentinel posted by destructor

			auto* base { reinterpret_cast< WinAwaiterBase* >( ovl ) };
			const DWORD bytes { entries[ i ].dwNumberOfBytesTransferred };
			const bool success { entries[ i ].lpOverlapped != nullptr };

			switch ( base->m_type )
			{
				case WinAwaiterBase::Type::READ:
					static_cast< ReadAwaiterWin* >( base )->complete( bytes, success );
					break;
				case WinAwaiterBase::Type::WRITE:
					static_cast< WriteAwaiterWin* >( base )->complete( bytes, success );
					break;
				default:
					log::error( "IOUringW10: unknown awaiter type in completion" );
					break;
			}
		}
	}

	log::info( "IOUringW10 (IOCP) watcher thread exiting" );
}

IOUringW10::IOUringW10() :
  m_running( std::make_shared< std::atomic< bool > >( false ) ),
  m_watcher_thread( &iocpWatcherThread, this, m_running )
{
	m_iocp = CreateIoCompletionPort( INVALID_HANDLE_VALUE, nullptr, 0, 1 );

	if ( m_iocp == INVALID_HANDLE_VALUE || m_iocp == nullptr )
	{
		log::error( "Failed to create IOCP, error: {}", GetLastError() );
		throw std::runtime_error( "IOUringW10: CreateIoCompletionPort failed" );
	}

	s_instance = this;
	m_running->store( true );
	m_running->notify_all();
}

IOUringW10::~IOUringW10()
{
	m_watcher_thread.request_stop();
	// Unblock the watcher thread (500 ms timeout means it will exit on its own soon)
	m_watcher_thread.join();
	if ( m_iocp && m_iocp != INVALID_HANDLE_VALUE ) CloseHandle( m_iocp );
	log::info( "IOUringW10 (IOCP) watcher thread joined" );
}

void IOUringW10::associateHandle( const NativeHandle handle )
{
	const HANDLE h { reinterpret_cast< HANDLE >( handle ) };
	if ( CreateIoCompletionPort( h, m_iocp, reinterpret_cast< ULONG_PTR >( h ), 0 ) == nullptr )
		throw std::runtime_error(
			"IOUringW10::associateHandle: CreateIoCompletionPort failed, error: " +
			std::to_string( GetLastError() ) );
}

drogon::Task< std::vector< std::byte > >
	IOUringW10::read( const NativeHandle handle, const std::size_t offset, const std::size_t len )
{
	const HANDLE h { reinterpret_cast< HANDLE >( handle ) };

	auto buffer { std::make_shared< std::vector< std::byte > >() };
	buffer->resize( len );

	ReadAwaiterWin awaiter { buffer };
	awaiter.m_overlapped.Offset     = static_cast< DWORD >( offset & 0xFFFFFFFF );
	awaiter.m_overlapped.OffsetHigh = static_cast< DWORD >( ( offset >> 32 ) & 0xFFFFFFFF );

	const BOOL ok {
		ReadFile( h, buffer->data(), static_cast< DWORD >( len ), nullptr, &awaiter.m_overlapped )
	};

	if ( !ok )
	{
		const DWORD err { GetLastError() };
		if ( err != ERROR_IO_PENDING )
		{
			log::error( "ReadFile failed immediately, error: {}", err );
			throw std::runtime_error( "IOUringW10::read: ReadFile failed" );
		}
		// ERROR_IO_PENDING: normal async path, awaiter will be resumed by watcher thread
	}
	else
	{
		// Completed synchronously; Windows still posts a completion packet to IOCP so the
		// watcher thread will call complete(). Just suspend and let it do so.
	}

	co_return co_await awaiter;
}

drogon::Task< void >
	IOUringW10::write( const NativeHandle handle, std::vector< std::byte > data, const std::size_t offset )
{
	const HANDLE h { reinterpret_cast< HANDLE >( handle ) };

	WriteAwaiterWin awaiter {};
	awaiter.m_overlapped.Offset     = static_cast< DWORD >( offset & 0xFFFFFFFF );
	awaiter.m_overlapped.OffsetHigh = static_cast< DWORD >( ( offset >> 32 ) & 0xFFFFFFFF );

	const BOOL ok {
		WriteFile( h, data.data(), static_cast< DWORD >( data.size() ), nullptr, &awaiter.m_overlapped )
	};

	if ( !ok )
	{
		const DWORD err { GetLastError() };
		if ( err != ERROR_IO_PENDING )
		{
			log::error( "WriteFile failed immediately, error: {}", err );
			throw std::runtime_error( "IOUringW10::write: WriteFile failed" );
		}
	}

	co_await awaiter;
}

IOUringW10& IOUringW10::getW10Instance()
{
	if ( !s_instance ) throw std::runtime_error( "IOUringW10 not initialised" );
	return *s_instance;
}

} // namespace idhan

#endif // _WIN32
