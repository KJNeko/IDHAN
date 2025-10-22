//
// Created by kj16609 on 7/29/25.
//
#pragma once

#include <liburing/io_uring.h>
#include <sys/mman.h>

#include <filesystem>

#include "ReadAwaiter.hpp"
#include "WriteAwaiter.hpp"
#include "drogon/utils/coroutine.h"
#include "fgl/defines.hpp"

namespace idhan
{

class FileIOUring
{
	struct FileDescriptor
	{
		std::shared_ptr< int > m_fd;

		FileDescriptor() = delete;

		explicit FileDescriptor( const int fd );

		operator int() const;
	};

	FileDescriptor m_fd { -1 };
	std::size_t m_size { 0 };
	std::filesystem::path m_path;
	void* m_mmap_ptr { nullptr };

  public:

	FileIOUring( const std::filesystem::path& path );

	~FileIOUring();

	std::size_t size() const;

	const std::filesystem::path& path() const;

	drogon::Task< std::vector< std::byte > > readAll() const;

	drogon::Task< std::vector< std::byte > > read( std::size_t offset, std::size_t len ) const;
	drogon::Task< void > write( std::vector< std::byte > data, std::size_t offset = 0 ) const;

	drogon::Task< std::vector< std::byte > > fallbackRead( std::size_t offset, std::size_t len ) const;
	drogon::Task< void > fallbackWrite( std::vector< std::byte > data, std::size_t size ) const;

	//! Mmaps the file into memory, Read only
	std::pair< void*, std::size_t > mmap();

	FileIOUring() = delete;
	FileIOUring( const FileIOUring& );
	FileIOUring& operator=( const FileIOUring& );
	FileIOUring( FileIOUring&& );
	FileIOUring& operator=( FileIOUring&& );
};

struct IOUringUserData
{
	enum class Type
	{
		READ,
		WRITE
	} m_type;

	union
	{
		ReadAwaiter* read_awaiter;
		WriteAwaiter* write_awaiter;
	};

	IOUringUserData( ReadAwaiter* read ) : m_type( Type::READ ), read_awaiter( read ) {}

	IOUringUserData( WriteAwaiter* write ) : m_type( Type::WRITE ), write_awaiter( write ) {}

	~IOUringUserData() = default;
};

class IOUring
{
	io_uring_params m_params {};
	int uring_fd { 0 };
	std::shared_ptr< std::atomic< bool > > io_run { std::make_shared< std::atomic< bool > >( false ) };

	int setupUring();

  public:

	bool m_iouring_setup { false };

	std::mutex mtx {};

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

  private:

	io_uring_sqe* m_submission_entries { nullptr };

	SubmissionRingPointers setupSubmissionRing();
	CommandRingPointers setupCommandRing();
	void* setupSubmissionEntries() const;

	inline static IOUring* instance { nullptr };

	std::jthread io_thread;

	friend void
		ioThread( const std::stop_token& token, IOUring* uring, std::shared_ptr< std::atomic< bool > > running );

	std::atomic< unsigned > to_submit { 0 };

	//! Submits an empty operation to io_uring to unstick the thread to allow it to properly exit
	void sendNop();

  public:

	FGL_DELETE_COPY( IOUring );
	FGL_DELETE_MOVE( IOUring );

	static IOUring& getInstance();

	void notifySubmit( unsigned int count ) const;

	explicit IOUring();

	WriteAwaiter sendWrite( const io_uring_sqe& sqe );
	ReadAwaiter sendRead( const io_uring_sqe& sqe );

	~IOUring();
};

} // namespace idhan