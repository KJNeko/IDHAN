#include "startup/Preflight.hpp"

#include <array>
#include <exception>
#include <string>
#include <string_view>
#include <vector>

#include "logging/log.hpp"

#ifdef __linux__

#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/wait.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <expected>
#include <fcntl.h>
#include <fstream>
#include <liburing.h>
#include <unistd.h>

#include "idhan/errnoMessage.hpp"
#include "ipc/UniqueFd.hpp"
#include "logging/format_ns.hpp"

#endif

namespace idhan::startup
{

#ifdef __linux__

using ProbeResult = std::expected< void, std::string >;

struct Capability
{
	std::string_view name;
	std::string_view consequence;
	ProbeResult ( *probe )();
};

static ProbeResult probeAnonymousMapping()
{
	constexpr std::size_t length { 4096 };

	void* const mapping { ::mmap( nullptr, length, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 ) };
	if ( mapping == MAP_FAILED ) return std::unexpected( errnoMessage( "mmap failed" ) );

	*static_cast< volatile std::byte* >( mapping ) = std::byte { 0 };

	if ( ::munmap( mapping, length ) != 0 ) return std::unexpected( errnoMessage( "munmap failed" ) );

	return {};
}

static ProbeResult probeMemfd()
{
	const ipc::UniqueFd fd { ::memfd_create( "idhan-preflight", MFD_CLOEXEC | MFD_ALLOW_SEALING ) };
	if ( !fd ) return std::unexpected( errnoMessage( "memfd_create failed" ) );

	if ( ::ftruncate( fd.get(), 4096 ) != 0 ) return std::unexpected( errnoMessage( "ftruncate failed" ) );

	return {};
}

static ProbeResult probeSharedMapping()
{
	constexpr std::size_t length { 4096 };

	const ipc::UniqueFd fd { ::memfd_create( "idhan-preflight", MFD_CLOEXEC ) };
	if ( !fd ) return std::unexpected( errnoMessage( "memfd_create failed" ) );

	if ( ::ftruncate( fd.get(), length ) != 0 ) return std::unexpected( errnoMessage( "ftruncate failed" ) );

	void* const mapping { ::mmap( nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0 ) };
	if ( mapping == MAP_FAILED ) return std::unexpected( errnoMessage( "mmap failed" ) );

	if ( ::munmap( mapping, length ) != 0 ) return std::unexpected( errnoMessage( "munmap failed" ) );

	return {};
}

static ProbeResult probeSealing()
{
	const ipc::UniqueFd fd { ::memfd_create( "idhan-preflight", MFD_CLOEXEC | MFD_ALLOW_SEALING ) };
	if ( !fd ) return std::unexpected( errnoMessage( "memfd_create failed" ) );

	constexpr unsigned int seals { F_SEAL_WRITE | F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_SEAL };

	if ( ::fcntl( fd.get(), F_ADD_SEALS, seals ) != 0 ) return std::unexpected( errnoMessage( "F_ADD_SEALS failed" ) );

	return {};
}

static std::string readIoUringSysctl()
{
	std::ifstream ifs { "/proc/sys/kernel/io_uring_disabled" };
	if ( !ifs ) return {};

	std::string value {};
	ifs >> value;

	return value;
}

static ProbeResult probeIoUring()
{
	io_uring_params params {};

	const int ring { io_uring_setup( 1, &params ) };
	if ( ring < 0 )
	{
		const auto sysctl { readIoUringSysctl() };
		const bool disabled_by_sysctl { sysctl == "1" || sysctl == "2" };

		if ( disabled_by_sysctl )
			return std::unexpected(
				format_ns::format(
					"io_uring_setup failed: {} (kernel.io_uring_disabled is {})", std::strerror( -ring ), sysctl ) );

		return std::unexpected( format_ns::format( "io_uring_setup failed: {}", std::strerror( -ring ) ) );
	}

	const ipc::UniqueFd fd { ring };

	const std::size_t length { params.sq_off.array + params.sq_entries * sizeof( unsigned ) };

	void* const mapping {
		::mmap( nullptr, length, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, ring, IORING_OFF_SQ_RING )
	};
	if ( mapping == MAP_FAILED ) return std::unexpected( errnoMessage( "mapping the submission ring failed" ) );

	if ( ::munmap( mapping, length ) != 0 ) return std::unexpected( errnoMessage( "munmap failed" ) );

	if ( const int entered { io_uring_enter( static_cast< unsigned int >( ring ), 0, 0, 0, nullptr ) }; entered < 0 )
		return std::unexpected( format_ns::format( "io_uring_enter failed: {}", std::strerror( -entered ) ) );

	return {};
}

static ProbeResult probeDescriptorPassing()
{
	constexpr std::size_t control_capacity { CMSG_SPACE( sizeof( int ) ) };

	std::array< int, 2 > sockets { { -1, -1 } };
	if ( ::socketpair( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets.data() ) != 0 )
		return std::unexpected( errnoMessage( "socketpair failed" ) );

	const ipc::UniqueFd sender { sockets[ 0 ] };
	const ipc::UniqueFd receiver { sockets[ 1 ] };

	const ipc::UniqueFd payload { ::open( "/dev/null", O_RDONLY | O_CLOEXEC ) };
	if ( !payload ) return std::unexpected( errnoMessage( "opening /dev/null failed" ) );

	std::byte body { std::byte { 0 } };
	iovec iov { .iov_base = &body, .iov_len = sizeof( body ) };

	std::array< std::byte, control_capacity > control {};

	msghdr message {};
	message.msg_iov = &iov;
	message.msg_iovlen = 1;
	message.msg_control = control.data();
	message.msg_controllen = control_capacity;

	cmsghdr* const header { CMSG_FIRSTHDR( &message ) };
	header->cmsg_level = SOL_SOCKET;
	header->cmsg_type = SCM_RIGHTS;
	header->cmsg_len = CMSG_LEN( sizeof( int ) );

	const int sent { payload.get() };
	std::memcpy( CMSG_DATA( header ), &sent, sizeof( int ) );

	if ( ::sendmsg( sender.get(), &message, MSG_NOSIGNAL ) < 0 )
		return std::unexpected( errnoMessage( "sendmsg with SCM_RIGHTS failed" ) );

	std::byte received_body { std::byte { 0 } };
	iovec received_iov { .iov_base = &received_body, .iov_len = sizeof( received_body ) };

	std::array< std::byte, control_capacity > received_control {};

	msghdr received {};
	received.msg_iov = &received_iov;
	received.msg_iovlen = 1;
	received.msg_control = received_control.data();
	received.msg_controllen = control_capacity;

	if ( ::recvmsg( receiver.get(), &received, MSG_CMSG_CLOEXEC ) < 0 )
		return std::unexpected( errnoMessage( "recvmsg failed" ) );

	const cmsghdr* const received_header { CMSG_FIRSTHDR( &received ) };

	const bool carried_descriptor {
		received_header != nullptr && received_header->cmsg_level == SOL_SOCKET
		&& received_header->cmsg_type == SCM_RIGHTS
	};

	if ( !carried_descriptor ) return std::unexpected( "the descriptor sent over the socket did not arrive" );

	int arrived { -1 };
	std::memcpy( &arrived, CMSG_DATA( received_header ), sizeof( int ) );

	const ipc::UniqueFd owned { arrived };

	return {};
}

static ProbeResult probeEventfd()
{
	const ipc::UniqueFd fd { ::eventfd( 0, EFD_CLOEXEC | EFD_NONBLOCK ) };
	if ( !fd ) return std::unexpected( errnoMessage( "eventfd failed" ) );

	constexpr std::uint64_t one { 1 };
	if ( ::write( fd.get(), &one, sizeof( one ) ) != sizeof( one ) )
		return std::unexpected( errnoMessage( "writing to the eventfd failed" ) );

	std::uint64_t drained { 0 };
	if ( ::read( fd.get(), &drained, sizeof( drained ) ) != sizeof( drained ) )
		return std::unexpected( errnoMessage( "reading from the eventfd failed" ) );

	return {};
}

static ProbeResult probeFork()
{
	const pid_t pid { ::fork() };
	if ( pid < 0 ) return std::unexpected( errnoMessage( "fork failed" ) );

	if ( pid == 0 ) ::_exit( 0 );

	int status { 0 };
	while ( ::waitpid( pid, &status, 0 ) < 0 )
	{
		if ( errno != EINTR ) return std::unexpected( errnoMessage( "waitpid failed" ) );
	}

	const bool exited_cleanly { WIFEXITED( status ) && WEXITSTATUS( status ) == 0 };

	if ( !exited_cleanly ) return std::unexpected( "the forked child did not exit cleanly" );

	return {};
}

static ProbeResult probeCloseRange()
{
	ipc::UniqueFd victim { ::open( "/dev/null", O_RDONLY | O_CLOEXEC ) };
	if ( !victim ) return std::unexpected( errnoMessage( "opening /dev/null failed" ) );

	const auto fd { static_cast< unsigned int >( victim.get() ) };

	if ( ::close_range( fd, fd, 0 ) != 0 ) return std::unexpected( errnoMessage( "close_range failed" ) );

	[[maybe_unused]] const int closed { victim.release() };

	return {};
}

static ProbeResult probePrctl()
{
	if ( ::prctl( PR_GET_DUMPABLE, 0, 0, 0, 0 ) < 0 )
		return std::unexpected( errnoMessage( "PR_GET_DUMPABLE failed" ) );

	if ( ::prctl( PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0 ) < 0 )
		return std::unexpected( errnoMessage( "PR_GET_NO_NEW_PRIVS failed" ) );

	int signal { 0 };
	if ( ::prctl( PR_GET_PDEATHSIG, &signal, 0, 0, 0 ) < 0 )
		return std::unexpected( errnoMessage( "PR_GET_PDEATHSIG failed" ) );

	return {};
}

static ProbeResult probeProcfs()
{
	std::ifstream statm { "/proc/self/statm" };
	if ( !statm ) return std::unexpected( "/proc/self/statm is not readable" );

	std::size_t pages { 0 };
	if ( !( statm >> pages ) ) return std::unexpected( "/proc/self/statm could not be parsed" );

	return {};
}

static ProbeResult probeCoreLimit()
{
	rlimit limit {};
	if ( ::getrlimit( RLIMIT_CORE, &limit ) != 0 ) return std::unexpected( errnoMessage( "getrlimit failed" ) );

	if ( ::setrlimit( RLIMIT_CORE, &limit ) != 0 ) return std::unexpected( errnoMessage( "setrlimit failed" ) );

	return {};
}

static constexpr std::array capabilities {
	Capability { "anonymous memory mapping", "every allocation path IDHAN has", probeAnonymousMapping },
	Capability { "memfd_create", "module IPC buffers and generator output", probeMemfd },
	Capability { "memfd sealing", "handing module output back to the server", probeSealing },
	Capability { "shared file mapping", "module IPC and the io_uring rings", probeSharedMapping },
	Capability { "unix socket descriptor passing", "every module call", probeDescriptorPassing },
	Capability { "eventfd", "the module and downloader IO loops", probeEventfd },
	Capability { "fork", "spawning module workers", probeFork },
	Capability { "close_range", "spawning module workers", probeCloseRange },
	Capability { "prctl", "module worker lifetime and hardening", probePrctl },
	Capability { "procfs", "worker RSS accounting and executable discovery", probeProcfs },
	Capability { "resource limits", "dropping core dumps in hardened workers", probeCoreLimit },
	Capability { "io_uring", "asynchronous file IO", probeIoUring },
};

void runPreflight( const bool force_start )
{
	log::debug( "Preflight: checking kernel capabilities" );

	std::vector< std::string_view > missing {};

	for ( const auto& capability : capabilities )
	{
		const auto result { capability.probe() };

		if ( result )
		{
			log::debug( "Preflight: {} available", capability.name );
			continue;
		}

		log::critical(
			"Preflight: {} is unavailable ({}), Which IDHAN needs for {}",
			capability.name,
			result.error(),
			capability.consequence );

		missing.emplace_back( capability.name );
	}

	if ( missing.empty() )
	{
		log::info( "Preflight: every kernel capability IDHAN needs is present" );
		return;
	}

	std::string names {};
	for ( const auto& name : missing )
	{
		if ( !names.empty() ) names += ", ";
		names += name;
	}

	log::critical( "Preflight failed. Missing: {}", names );
	log::critical(
		"A container runtime blocking these usually means the seccomp profile is too strict. Try running with seccomp=unconfined, or allow the syscalls above" );

	if ( force_start )
	{
		log::warn( "force_start is set, Starting anyway" );
		return;
	}

	log::critical( "Aborting. Pass --force_start=true to start regardless" );
	std::terminate();
}

#else

void runPreflight( [[maybe_unused]] const bool force_start )
{
	log::debug( "Preflight: no capability probes for this platform" );
}

#endif

} // namespace idhan::startup
