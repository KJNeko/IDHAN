//
// Created by kj16609 on 7/28/26.
//
#pragma once

#include <json/value.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "UniqueFd.hpp"

namespace idhan::ipc
{

//! One protocol message: a JSON body plus whatever descriptors were attached to it.
struct Frame
{
	Json::Value body {};
	//! Descriptors sent alongside the body, owned by this frame. Typically one blob, often none.
	std::vector< UniqueFd > fds {};
};

//! Ceiling on a frame body, as a guard against a corrupt length turning into a huge allocation.
/** Bodies are JSON control messages -- bulk data travels as a blob descriptor, never inline -- so
 *  this is far above anything legitimate. The one body that grows with the data is an archive's
 *  extra JSON, which carries a hash and a filename per member; even a pathological archive lands
 *  orders of magnitude below this. */
inline constexpr std::uint32_t MAX_FRAME_BODY { 64u * 1024u * 1024u };

//! Most descriptors one frame may carry. The protocol never needs more than one.
inline constexpr std::size_t MAX_FRAME_FDS { 8 };

//! Creates the connected socket pair backing a worker channel.
/** SOCK_STREAM with explicit length framing rather than SOCK_SEQPACKET: a datagram socket caps a
 *  message at the socket buffer size, which an archive's per-member extra JSON can genuinely
 *  exceed. Framing it by hand costs an eight-byte header and removes the ceiling. */
[[nodiscard]] std::expected< std::pair< UniqueFd, UniqueFd >, std::string > createChannel();

//! Puts \p sock into non-blocking mode. FrameReader requires this: it drains until the socket says
//! it has nothing left, which on a blocking socket would mean waiting forever for a frame that has
//! not been sent yet.
[[nodiscard]] std::expected< void, std::string > setNonBlocking( int sock );

//! Writes one frame, blocking until it is fully handed to the kernel.
/** \param fds Descriptors to attach. Borrowed -- the caller keeps ownership, and the receiver gets
 *             its own copies via SCM_RIGHTS.
 *
 *  Blocking is deliberate even on the server's event loop. Bodies are small, the socket buffer
 *  absorbs many of them, and the alternative -- an outbound queue per worker -- buys nothing until
 *  a worker stops reading, which is a case the liveness watchdog already handles by killing it. */
[[nodiscard]] std::expected< void, std::string > sendFrame(
	int sock,
	const Json::Value& body,
	std::span< const int > fds = {} );

//! Reassembles frames from a socket that may deliver them in arbitrary pieces.
/** Kept as an object with its own state because both sides need to read without blocking: the
 *  server reads on a trantor event loop and the runner's IO thread must stay responsive to keep
 *  answering heartbeats while a module call is in flight. */
class FrameReader
{
	//! body length and descriptor count, in native byte order -- both ends are the same build on
	//! the same machine, so there is nothing to negotiate.
	static constexpr std::size_t HEADER_SIZE { 8 };

	std::array< std::byte, HEADER_SIZE > m_header {};
	std::size_t m_header_filled { 0 };

	std::vector< std::byte > m_body {};
	std::size_t m_body_filled { 0 };
	std::uint32_t m_body_expected { 0 };
	std::uint32_t m_fds_expected { 0 };
	bool m_header_complete { false };

	//! Descriptors arrive attached to the header, so they are held here until the body that
	//! describes them has been read.
	std::vector< UniqueFd > m_pending_fds {};

	bool m_eof { false };

  public:

	//! Consumes everything currently readable on \p sock and returns the frames that completed.
	/** Returns an empty vector when the socket had nothing to give (or only a partial frame); that
	 *  is the normal case, not an error. Check atEof() afterwards to distinguish "nothing yet" from
	 *  "the peer is gone". */
	[[nodiscard]] std::expected< std::vector< Frame >, std::string > read( int sock );

	//! True once the peer has closed its end. Any frame in flight at that point is discarded.
	[[nodiscard]] bool atEof() const { return m_eof; }
};

//! Queues outbound frames and drains them without ever blocking.
/** The counterpart to sendFrame, for the side that cannot afford to block. If both ends blocked on
 *  full socket buffers at the same time -- each waiting for the other to read -- neither would ever
 *  read again. The server uses this so that cycle cannot close: it always drains its read side, so
 *  the worker's blocking writes always complete.
 *
 *  Not internally synchronised. Enqueue and drain from one thread, or hold a lock across both. */
class FrameWriter
{
	struct Pending
	{
		//! Header and body, already serialised, so a partial write just advances an offset.
		std::vector< std::byte > buffer {};
		//! Descriptors to attach, owned until they are handed to the kernel.
		std::vector< UniqueFd > fds {};
		std::size_t sent { 0 };
	};

	std::deque< Pending > m_queue {};

  public:

	//! Serialises \p body and queues it. Descriptors in \p fds are duplicated, so the caller's
	//! ownership (typically a Blob that may be destroyed before the frame goes out) is untouched.
	[[nodiscard]] std::expected< void, std::string > enqueue(
		const Json::Value& body,
		std::span< const int > fds = {} );

	//! Writes as much as the socket will take.
	/** \return true when the queue is empty, false when the socket filled up and the caller should
	 *          wait for writability before calling again. */
	[[nodiscard]] std::expected< bool, std::string > drain( int sock );

	[[nodiscard]] bool empty() const { return m_queue.empty(); }
};

//! Blocks until one whole frame arrives, or \p timeout elapses.
/** Only for the startup interrogation, where the host has nothing else to do but wait for a
 *  manifest and must not hang forever on a library that wedges in a static initialiser. */
[[nodiscard]] std::expected< Frame, std::string > readFrameBlocking( int sock, std::chrono::milliseconds timeout );

} // namespace idhan::ipc
