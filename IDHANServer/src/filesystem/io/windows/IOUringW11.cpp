//
// Created by kj16609 on 7/29/25.
//
#ifdef _WIN32

#include "filesystem/io/windows/IOUringW11.hpp"

#include "filesystem/io/windows/WinAwaiters.hpp"
#include "logging/log.hpp"
#include "trantor/net/EventLoop.h"

namespace idhan
{

void ioRingWatcherThread(
	const std::stop_token& token,
	IOUringW11* backend,
	std::shared_ptr< std::atomic< bool > > running )
{
	log::info( "IOUringW11 (IoRing) watcher thread started" );

	if ( running->load() == false ) running->wait( false );

	while ( !token.stop_requested() )
	{
		// Wait for a completion event (signaled by the kernel when completions are available)
		const DWORD wait_result { WaitForSingleObject( backend->m_completion_event, 500 /*ms*/ ) };

		if ( wait_result == WAIT_TIMEOUT ) continue;

		if ( wait_result != WAIT_OBJECT_0 )
		{
			log::error( "IOUringW11: WaitForSingleObject failed, error: {}", GetLastError() );
			continue;
		}

		IORING_CQE cqe {};
		while ( PopIoRingCompletion( backend->m_ring, &cqe ) == S_OK )
		{
			if ( cqe.UserData == 0 ) continue; // sentinel / NOP

			auto* base { reinterpret_cast< WinAwaiterBase* >( static_cast< void* >( cqe.UserData ) ) };
			const bool success { SUCCEEDED( cqe.ResultCode ) };
			const DWORD bytes { success ? static_cast< DWORD >( cqe.Information ) : 0 };

			switch ( base->m_type )
			{
				case WinAwaiterBase::Type::READ:
					static_cast< ReadAwaiterWin* >( base )->complete( bytes, success );
					break;
				case WinAwaiterBase::Type::WRITE:
					static_cast< WriteAwaiterWin* >( base )->complete( bytes, success );
					break;
				default:
					log::error( "IOUringW11: unknown awaiter type in completion" );
					break;
			}
		}
	}

	log::info( "IOUringW11 (IoRing) watcher thread exiting" );
}

bool IOUringW11::isAvailable()
{
	HIORING test_ring { nullptr };
	const IORING_CREATE_FLAGS flags {};
	const HRESULT hr { CreateIoRing( IORING_VERSION_3, flags, 8, 16, &test_ring ) };
	if ( SUCCEEDED( hr ) )
	{
		CloseIoRing( test_ring );
		return true;
	}
	return false;
}

IOUringW11::IOUringW11() :
  m_running( std::make_shared< std::atomic< bool > >( false ) )
{
	const IORING_CREATE_FLAGS flags {};
	if ( const HRESULT hr { CreateIoRing( IORING_VERSION_3, flags, 64, 128, &m_ring ) }; FAILED( hr ) )
		throw std::runtime_error( "IOUringW11: CreateIoRing failed" );

	m_completion_event = CreateEvent( nullptr, FALSE, FALSE, nullptr );
	if ( m_completion_event == INVALID_HANDLE_VALUE )
		throw std::runtime_error( "IOUringW11: CreateEvent failed" );

	if ( const HRESULT hr { SetIoRingCompletionEvent( m_ring, m_completion_event ) }; FAILED( hr ) )
		throw std::runtime_error( "IOUringW11: SetIoRingCompletionEvent failed" );

	m_watcher_thread = std::jthread( &ioRingWatcherThread, this, m_running );

	s_instance = this;
	m_running->store( true );
	m_running->notify_all();
}

IOUringW11::~IOUringW11()
{
	m_watcher_thread.request_stop();
	SetEvent( m_completion_event ); // unblock the watcher thread
	m_watcher_thread.join();
	log::info( "IOUringW11 (IoRing) watcher thread joined" );

	if ( m_ring ) CloseIoRing( m_ring );
	if ( m_completion_event != INVALID_HANDLE_VALUE ) CloseHandle( m_completion_event );
}

drogon::Task< std::vector< std::byte > >
	IOUringW11::read( const NativeHandle handle, const std::size_t offset, const std::size_t len )
{
	const HANDLE h { reinterpret_cast< HANDLE >( handle ) };

	auto buffer { std::make_shared< std::vector< std::byte > >() };
	buffer->resize( len );

	ReadAwaiterWin awaiter { buffer };

	const IORING_HANDLE_REF file_ref { IoRingHandleRefFromHandle( h ) };
	const IORING_BUFFER_REF buf_ref { IoRingBufferRefFromPointer( buffer->data() ) };

	if ( const HRESULT hr { BuildIoRingReadFile(
			 m_ring,
			 file_ref,
			 buf_ref,
			 static_cast< UINT32 >( len ),
			 offset,
			 reinterpret_cast< UINT_PTR >( &awaiter ),
			 IOSQE_FLAGS_NONE ) };
		 FAILED( hr ) )
	{
		throw std::runtime_error( "IOUringW11::read: BuildIoRingReadFile failed" );
	}

	// Submit without waiting; watcher thread will receive the completion
	SubmitIoRing( m_ring, 0, 0, nullptr );

	co_return co_await awaiter;
}

drogon::Task< void >
	IOUringW11::write( const NativeHandle handle, std::vector< std::byte > data, const std::size_t offset )
{
	const HANDLE h { reinterpret_cast< HANDLE >( handle ) };

	WriteAwaiterWin awaiter {};

	const IORING_HANDLE_REF file_ref { IoRingHandleRefFromHandle( h ) };
	const IORING_BUFFER_REF buf_ref { IoRingBufferRefFromPointer( data.data() ) };

	if ( const HRESULT hr { BuildIoRingWriteFile(
			 m_ring,
			 file_ref,
			 buf_ref,
			 static_cast< UINT32 >( data.size() ),
			 offset,
			 FILE_WRITE_FLAGS_NONE,
			 reinterpret_cast< UINT_PTR >( &awaiter ),
			 IOSQE_FLAGS_NONE ) };
		 FAILED( hr ) )
	{
		throw std::runtime_error( "IOUringW11::write: BuildIoRingWriteFile failed" );
	}

	SubmitIoRing( m_ring, 0, 0, nullptr );

	co_await awaiter;
}

IOUringW11& IOUringW11::getW11Instance()
{
	if ( !s_instance ) throw std::runtime_error( "IOUringW11 not initialised" );
	return *s_instance;
}

} // namespace idhan

#endif // _WIN32
