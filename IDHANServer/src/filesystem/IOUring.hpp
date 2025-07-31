//
// Created by kj16609 on 7/29/25.
//
#pragma once

#include <liburing/io_uring.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <filesystem>
#include <liburing.h>

#include "drogon/utils/coroutine.h"
#include "fgl/defines.hpp"

namespace idhan
{

class IOUring;

struct [[nodiscard]] ReadAwaiter
{
	struct promise_type;

	using handle_type = std::coroutine_handle< promise_type >;

	bool await_ready() const noexcept { return false; }

	void await_suspend( const std::coroutine_handle<> h );

	std::vector< std::byte > await_resume();

	std::vector< std::byte > m_data {};
	std::exception_ptr m_exception {};
	handle_type m_h;
	std::coroutine_handle<> m_cont;
	IOUring* m_uring { nullptr };
	struct io_uring_sqe m_sqe {};

	ReadAwaiter( handle_type handle ) : m_h( handle ) {}

	ReadAwaiter( IOUring* uring, struct io_uring_sqe sqe );

	void complete( int result, const std::vector< std::byte >& data );

	~ReadAwaiter();
};

struct FileIOUring
{
	int m_fd { -1 };

	FileIOUring( const std::filesystem::path& path ) : m_fd( open( path.c_str(), O_RDWR ) ) {}

	drogon::Task< std::vector< std::byte > > read( std::size_t offset, std::size_t len );
};

class IOUring
{
	io_uring_params m_params {};
	int uring_fd { 0 };
	std::shared_ptr< std::atomic< bool > > io_run { std::make_shared< std::atomic< bool > >( false ) };

	static int setupUring( io_uring_params& params );

  public:

	struct SubmissionRingPointers
	{
		void* mmap { nullptr };
		std::size_t length { 0 };
		unsigned* head { nullptr };
		unsigned* tail { nullptr };
		unsigned* mask { nullptr };
		io_uring_sqe* entries { nullptr };
		unsigned* flags { nullptr };
		unsigned* dropped { nullptr };
		unsigned* array { nullptr };

		~SubmissionRingPointers()
		{
			if ( length > 0 ) munmap( mmap, length );
		}
	} m_submission_ring;

	struct CommandRingPointers
	{
		void* mmap { nullptr };
		std::size_t length { 0 };
		unsigned* head { nullptr };
		unsigned* tail { nullptr };
		unsigned* mask { nullptr };
		unsigned* overflow { nullptr };
		io_uring_cqe* cqes { nullptr };
		unsigned* flags { nullptr };

		~CommandRingPointers()
		{
			if ( length > 0 ) munmap( mmap, length );
		}
	} m_command_ring;

	io_uring_sqe* m_submission_entries { nullptr };

	SubmissionRingPointers setupSubmissionRing();
	CommandRingPointers setupCommandRing();
	void* setupSubmissionEntries() const;

	inline static IOUring* instance { nullptr };

	std::jthread io_thread;

	friend void
		ioThread( const std::stop_token& token, IOUring* uring, std::shared_ptr< std::atomic< bool > > running );

	std::atomic< unsigned > to_submit { 0 };

  public:

	FGL_DELETE_COPY( IOUring );
	FGL_DELETE_MOVE( IOUring );

	static IOUring& getInstance()
	{
		if ( !instance ) throw std::runtime_error( "IOUring instance not initialized" );
		return *instance;
	}

	void notifySubmit( std::size_t count )
	{
		if ( auto ret = io_uring_enter( uring_fd, count, 0, IORING_ENTER_SQ_WAKEUP, nullptr ); ret < 0 )
		{
			throw std::runtime_error( "Failed to enter io_uring" );
		}
	}

	explicit IOUring();

	ReadAwaiter send( struct io_uring_sqe sqe );

	~IOUring();
};

} // namespace idhan