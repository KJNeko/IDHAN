#ifdef __linux__

#include <json/reader.h>
#include <json/writer.h>
#include <sys/socket.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <format>
#include <memory>
#include <poll.h>
#include <unistd.h>
#include <utility>

#include "idhan/errnoMessage.hpp"
#include "ipc/Frame.hpp"

namespace idhan::ipc
{

//! Enough control-message space for MAX_FRAME_FDS descriptors.
constexpr std::size_t CMSG_CAPACITY { CMSG_SPACE( sizeof( int ) * MAX_FRAME_FDS ) };

//! Pulls any SCM_RIGHTS descriptors out of a received message into \p out.
/** Received descriptors are adopted immediately, before anything else can fail. A descriptor that
 *  arrives and is then dropped on an error path is a leak the peer cannot see or recover from. */
void collectFds( msghdr& message, std::vector< UniqueFd >& out )
{
	for ( cmsghdr* header { CMSG_FIRSTHDR( &message ) }; header != nullptr; header = CMSG_NXTHDR( &message, header ) )
	{
		if ( header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS ) continue;

		const std::size_t payload { header->cmsg_len - CMSG_LEN( 0 ) };
		const std::size_t count { payload / sizeof( int ) };

		const int* fds { reinterpret_cast< const int* >( CMSG_DATA( header ) ) };
		for ( std::size_t i = 0; i < count; ++i ) out.emplace_back( fds[ i ] );
	}
}

//! Writes every byte of \p data, retrying on interruption.
[[nodiscard]] std::expected< void, std::string > sendAll( const int sock, const std::span< const std::byte > data )
{
	std::size_t sent { 0 };

	while ( sent < data.size() )
	{
		// MSG_NOSIGNAL: writing to a worker that has already died must surface as EPIPE here, not
		// as a SIGPIPE that takes down the server.
		const ssize_t result { ::send( sock, data.data() + sent, data.size() - sent, MSG_NOSIGNAL ) };

		if ( result < 0 )
		{
			if ( errno == EINTR ) continue;
			return std::unexpected( errnoMessage( "writing frame body failed" ) );
		}

		sent += static_cast< std::size_t >( result );
	}

	return {};
}

std::expected< std::pair< UniqueFd, UniqueFd >, std::string > createChannel()
{
	std::array< int, 2 > sockets { { -1, -1 } };

	if ( ::socketpair( AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sockets.data() ) != 0 )
		return std::unexpected( errnoMessage( "socketpair failed" ) );

	return std::pair< UniqueFd, UniqueFd > { UniqueFd { sockets[ 0 ] }, UniqueFd { sockets[ 1 ] } };
}

std::expected< void, std::string > setNonBlocking( const int sock )
{
	const int flags { ::fcntl( sock, F_GETFL, 0 ) };
	if ( flags < 0 ) return std::unexpected( errnoMessage( "reading socket flags failed" ) );

	if ( ::fcntl( sock, F_SETFL, flags | O_NONBLOCK ) != 0 )
		return std::unexpected( errnoMessage( "setting socket non-blocking failed" ) );

	return {};
}

std::expected< void, std::string > sendFrame(
	const int sock,
	const Json::Value& body,
	const std::span< const int > fds )
{
	if ( fds.size() > MAX_FRAME_FDS )
		return std::unexpected(
			std::format( "frame carries {} descriptors, at most {} allowed", fds.size(), MAX_FRAME_FDS ) );

	Json::StreamWriterBuilder builder {};
	builder[ "indentation" ] = "";
	builder[ "commentStyle" ] = "None";
	const std::string payload { Json::writeString( builder, body ) };

	if ( payload.size() > MAX_FRAME_BODY )
		return std::unexpected(
			std::format( "frame body of {} bytes exceeds the {} byte limit", payload.size(), MAX_FRAME_BODY ) );

	Header header { .m_body_expected = static_cast< std::uint32_t >( payload.size() ),
		            .m_fds_expected = static_cast< std::uint32_t >( fds.size() ) };

	// The descriptors ride on the header, never the body. That is what lets the reader collect them
	// with a single recvmsg before it knows how long the body is.
	std::array< std::byte, CMSG_CAPACITY > control {};

	iovec iov { .iov_base = &header, .iov_len = sizeof( header ) };

	msghdr message {};
	message.msg_iov = &iov;
	message.msg_iovlen = 1;

	if ( !fds.empty() )
	{
		message.msg_control = control.data();
		message.msg_controllen = CMSG_SPACE( sizeof( int ) * fds.size() );

		cmsghdr* const header_cmsg { CMSG_FIRSTHDR( &message ) };
		header_cmsg->cmsg_level = SOL_SOCKET;
		header_cmsg->cmsg_type = SCM_RIGHTS;
		header_cmsg->cmsg_len = CMSG_LEN( sizeof( int ) * fds.size() );
		std::memcpy( CMSG_DATA( header_cmsg ), fds.data(), sizeof( int ) * fds.size() );
	}

	ssize_t header_sent { 0 };
	while ( true )
	{
		header_sent = ::sendmsg( sock, &message, MSG_NOSIGNAL );
		if ( header_sent >= 0 ) break;
		if ( errno == EINTR ) continue;
		return std::unexpected( errnoMessage( "writing frame header failed" ) );
	}

	// A short header write is vanishingly unlikely at eight bytes, but if it happens the descriptors
	// have already been handed over with the first byte -- the remainder is plain data.
	if ( static_cast< std::size_t >( header_sent ) < sizeof( header ) )
	{
		const std::span< const std::byte > header_bytes {
			reinterpret_cast< const std::byte* >( &header ), sizeof( header )
		};
		if ( auto result { sendAll( sock, header_bytes.subspan( static_cast< std::size_t >( header_sent ) ) ) };
		     !result )
			return result;
	}

	return sendAll(
		sock, std::span< const std::byte > { reinterpret_cast< const std::byte* >( payload.data() ), payload.size() } );
}

std::expected< void, std::string > FrameWriter::enqueue( const Json::Value& body, const std::span< const int > fds )
{
	if ( fds.size() > MAX_FRAME_FDS )
		return std::unexpected(
			std::format( "frame carries {} descriptors, at most {} allowed", fds.size(), MAX_FRAME_FDS ) );

	Json::StreamWriterBuilder builder {};
	builder[ "indentation" ] = "";
	builder[ "commentStyle" ] = "None";
	const std::string payload { Json::writeString( builder, body ) };

	if ( payload.size() > MAX_FRAME_BODY )
		return std::unexpected(
			std::format( "frame body of {} bytes exceeds the {} byte limit", payload.size(), MAX_FRAME_BODY ) );

	Pending pending {};

	// Duplicated rather than borrowed: the frame may sit in this queue after the Blob that owned the
	// descriptor is long gone.
	for ( const int fd : fds )
	{
		UniqueFd copy { ::fcntl( fd, F_DUPFD_CLOEXEC, 0 ) };
		if ( !copy ) return std::unexpected( errnoMessage( "duplicating a descriptor for sending failed" ) );
		pending.fds.emplace_back( std::move( copy ) );
	}

	const Header header { .m_body_expected = static_cast< std::uint32_t >( payload.size() ),
		                  .m_fds_expected = static_cast< std::uint32_t >( fds.size() ) };

	pending.buffer.resize( sizeof( header ) + payload.size() );
	std::memcpy( pending.buffer.data(), &header, sizeof( header ) );
	std::memcpy( pending.buffer.data() + sizeof( header ), payload.data(), payload.size() );

	m_queue.emplace_back( std::move( pending ) );

	return {};
}

std::expected< bool, std::string > FrameWriter::drain( const int sock )
{
	while ( !m_queue.empty() )
	{
		Pending& pending { m_queue.front() };

		ssize_t written { 0 };

		if ( pending.sent == 0 && !pending.fds.empty() )
		{
			// The descriptors ride on the header, so the first write of a frame is the only one that
			// carries them.
			std::vector< int > raw {};
			raw.reserve( pending.fds.size() );
			for ( const auto& fd : pending.fds ) raw.emplace_back( fd.get() );

			std::array< std::byte, CMSG_CAPACITY > control {};
			iovec iov { .iov_base = pending.buffer.data(), .iov_len = sizeof( Header ) };

			msghdr message {};
			message.msg_iov = &iov;
			message.msg_iovlen = 1;
			message.msg_control = control.data();
			message.msg_controllen = CMSG_SPACE( sizeof( int ) * raw.size() );

			cmsghdr* const header { CMSG_FIRSTHDR( &message ) };
			header->cmsg_level = SOL_SOCKET;
			header->cmsg_type = SCM_RIGHTS;
			header->cmsg_len = CMSG_LEN( sizeof( int ) * raw.size() );
			std::memcpy( CMSG_DATA( header ), raw.data(), sizeof( int ) * raw.size() );

			written = ::sendmsg( sock, &message, MSG_NOSIGNAL | MSG_DONTWAIT );
		}
		else
		{
			written = ::send(
				sock,
				pending.buffer.data() + pending.sent,
				pending.buffer.size() - pending.sent,
				MSG_NOSIGNAL | MSG_DONTWAIT );
		}

		if ( written < 0 )
		{
			if ( errno == EINTR ) continue;
			// EWOULDBLOCK is EAGAIN on Linux, so testing both is redundant here.
			if ( errno == EAGAIN ) return false;
			return std::unexpected( errnoMessage( "writing frame failed" ) );
		}

		// Anything at all went out, so the ancillary data went with it; releasing the descriptors
		// here is what stops them being sent a second time on the next pass.
		if ( written > 0 ) pending.fds.clear();

		pending.sent += static_cast< std::size_t >( written );

		if ( pending.sent >= pending.buffer.size() ) m_queue.pop_front();
	}

	return true;
}

std::expected< std::vector< Frame >, std::string > FrameReader::read( const int sock )
{
	std::vector< Frame > completed {};

	while ( true )
	{
		std::byte* target { nullptr };
		std::size_t want { 0 };

		if ( !m_header_complete )
		{
			target = reinterpret_cast< std::byte* >( &m_header ) + m_header_filled;
			want = HEADER_SIZE - m_header_filled;
		}
		else if ( m_body_filled < m_header.m_body_expected )
		{
			target = m_body.data() + m_body_filled;
			want = m_header.m_body_expected - m_body_filled;
		}

		if ( want > 0 )
		{
			std::array< std::byte, CMSG_CAPACITY > control {};
			iovec iov { .iov_base = target, .iov_len = want };

			msghdr message {};
			message.msg_iov = &iov;
			message.msg_iovlen = 1;
			message.msg_control = control.data();
			message.msg_controllen = control.size();

			// MSG_CMSG_CLOEXEC: a received blob descriptor must not survive into a process this one
			// later execs.
			const ssize_t received { ::recvmsg( sock, &message, MSG_CMSG_CLOEXEC ) };

			if ( received < 0 )
			{
				if ( errno == EINTR ) continue;
				// EWOULDBLOCK is EAGAIN on Linux, so testing both is redundant here.
				if ( errno == EAGAIN ) break;
				return std::unexpected( errnoMessage( "reading frame failed" ) );
			}

			if ( received == 0 )
			{
				m_eof = true;
				break;
			}

			collectFds( message, m_pending_fds );

			if ( !m_header_complete )
				m_header_filled += static_cast< std::size_t >( received );
			else
				m_body_filled += static_cast< std::size_t >( received );
		}

		if ( !m_header_complete )
		{
			if ( m_header_filled < HEADER_SIZE ) continue;

			if ( m_header.m_body_expected > MAX_FRAME_BODY )
				return std::unexpected(
					std::format(
						"peer announced a {} byte frame body, over the {} byte limit",
						m_header.m_body_expected,
						MAX_FRAME_BODY ) );

			if ( m_header.m_fds_expected > MAX_FRAME_FDS )
				return std::unexpected(
					std::format(
						"peer announced {} descriptors, over the limit of {}",
						m_header.m_fds_expected,
						MAX_FRAME_FDS ) );

			m_body.assign( m_header.m_body_expected, std::byte { 0 } );
			m_body_filled = 0;
			m_header_complete = true;

			// A body of zero bytes never happens (jsoncpp always writes at least "null"), but the
			// loop must not depend on that to terminate.
			if ( m_header.m_body_expected > 0 ) continue;
		}

		if ( m_body_filled < m_header.m_body_expected ) continue;

		Json::Value parsed {};
		{
			Json::CharReaderBuilder reader_builder {};
			const std::unique_ptr< Json::CharReader > reader { reader_builder.newCharReader() };
			const auto* const begin { reinterpret_cast< const char* >( m_body.data() ) };

			std::string errors {};
			if ( !reader->parse( begin, begin + m_body.size(), &parsed, &errors ) )
				return std::unexpected( std::format( "frame body was not valid JSON: {}", errors ) );
		}

		if ( m_pending_fds.size() != m_header.m_fds_expected )
			return std::unexpected(
				std::format(
					"frame announced {} descriptors but {} arrived", m_header.m_fds_expected, m_pending_fds.size() ) );

		completed.emplace_back( Frame { .body = std::move( parsed ), .fds = std::move( m_pending_fds ) } );

		m_pending_fds.clear();
		m_header = {};
		m_header_filled = 0;
		m_header_complete = false;
		m_body.clear();
		m_body_filled = 0;
	}

	return completed;
}

std::expected< Frame, std::string > readFrameBlocking( const int sock, const std::chrono::milliseconds timeout )
{
	const auto deadline { std::chrono::steady_clock::now() + timeout };

	FrameReader reader {};

	while ( true )
	{
		auto frames { reader.read( sock ) };
		if ( !frames ) return std::unexpected( frames.error() );

		if ( !frames->empty() ) return std::move( frames->front() );

		if ( reader.atEof() )
			return std::unexpected( std::string { "peer closed the channel before sending a frame" } );

		const auto now { std::chrono::steady_clock::now() };
		if ( now >= deadline ) return std::unexpected( std::string { "timed out waiting for a frame" } );

		const auto remaining { std::chrono::duration_cast< std::chrono::milliseconds >( deadline - now ) };

		pollfd waiting { .fd = sock, .events = POLLIN, .revents = 0 };
		const int ready { ::poll( &waiting, 1, static_cast< int >( remaining.count() ) ) };

		if ( ready < 0 )
		{
			if ( errno == EINTR ) continue;
			return std::unexpected( errnoMessage( "poll on the worker channel failed" ) );
		}

		if ( ready == 0 ) return std::unexpected( std::string { "timed out waiting for a frame" } );
	}
}

} // namespace idhan::ipc

#endif
