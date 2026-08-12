#pragma once
#ifdef __linux__

#include <liburing/io_uring.h>
#include <sys/mman.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

#include "fgl/defines.hpp"
#include "filesystem/io/IOUring.hpp"

namespace idhan
{

struct ReadAwaiter;
struct WriteAwaiter;

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

	IOUringUserData( ReadAwaiter* r ) : m_type( Type::READ ), read_awaiter( r ) {}

	IOUringUserData( WriteAwaiter* w ) : m_type( Type::WRITE ), write_awaiter( w ) {}

	~IOUringUserData() = default;
};

class IOUringLinux final : public IOUring
{
	// Member order matters for initializer-list construction order
	io_uring_params m_params;
	// Signed: io_uring_setup() returns a negative errno on failure. A previous `unsigned` type made a
	// failed setup (e.g. -ENOMEM) read as a huge positive fd, so `uring_fd > 0` wrongly reported
	// success and the watcher spun on io_uring_enter with a bogus fd instead of using the sync fallback.
	int uring_fd;
	std::shared_ptr< std::atomic< bool > > io_run;

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

		~SubmissionRingPointers();
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

		~CommandRingPointers();
	} m_command_ring;

  private:

	io_uring_sqe* m_submission_entries { nullptr };

	SubmissionRingPointers setupSubmissionRing();
	CommandRingPointers setupCommandRing();
	void* setupSubmissionEntries() const;

	std::jthread io_thread;

	friend void ioThread(
		const std::stop_token& token,
		IOUringLinux* uring,
		std::shared_ptr< std::atomic< bool > > running );

	std::atomic< unsigned > to_submit { 0 };

	void sendNop();

	inline static IOUringLinux* s_instance { nullptr };

  public:

	FGL_DELETE_COPY( IOUringLinux );
	FGL_DELETE_MOVE( IOUringLinux );

	static IOUringLinux& getLinuxInstance();

	void notifySubmit( unsigned int count ) const;

	WriteAwaiter sendWrite( const io_uring_sqe& sqe );
	ReadAwaiter sendRead( const io_uring_sqe& sqe, std::shared_ptr< std::vector< std::byte > >& data );

	drogon::Task< std::vector< std::byte > > read( NativeHandle handle, std::size_t offset, std::size_t len ) override;

	drogon::Task< void > write( NativeHandle handle, std::vector< std::byte > data, std::size_t offset ) override;

	explicit IOUringLinux();
	~IOUringLinux() override;
};

} // namespace idhan

#endif // __linux__
