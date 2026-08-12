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

struct Frame
{
	Json::Value body {};
	std::vector< UniqueFd > fds {};
};

inline constexpr std::uint32_t MAX_FRAME_BODY { 64u * 1024u * 1024u };

inline constexpr std::size_t MAX_FRAME_FDS { 8 };

[[nodiscard]] std::expected< std::pair< UniqueFd, UniqueFd >, std::string > createChannel();

[[nodiscard]] std::expected< void, std::string > setNonBlocking( int sock );

[[nodiscard]] std::expected< void, std::string > sendFrame(
	int sock,
	const Json::Value& body,
	std::span< const int > fds = {} );

struct Header
{
	std::uint32_t m_body_expected;
	std::uint32_t m_fds_expected;
};

class FrameReader
{
	static constexpr std::size_t HEADER_SIZE { 8 };

	std::array< std::byte, HEADER_SIZE > m_header {};
	std::size_t m_header_filled { 0 };

	std::vector< std::byte > m_body {};
	std::size_t m_body_filled { 0 };
	std::uint32_t m_body_expected { 0 };
	std::uint32_t m_fds_expected { 0 };
	bool m_header_complete { false };

	std::vector< UniqueFd > m_pending_fds {};

	bool m_eof { false };

  public:

	[[nodiscard]] std::expected< std::vector< Frame >, std::string > read( int sock );

	[[nodiscard]] bool atEof() const { return m_eof; }
};

class FrameWriter
{
	struct Pending
	{
		std::vector< std::byte > buffer {};
		std::vector< UniqueFd > fds {};
		std::size_t sent { 0 };
	};

	std::deque< Pending > m_queue {};

  public:

	[[nodiscard]] std::expected< void, std::string > enqueue(
		const Json::Value& body,
		std::span< const int > fds = {} );

	[[nodiscard]] std::expected< bool, std::string > drain( int sock );

	[[nodiscard]] bool empty() const { return m_queue.empty(); }
};

[[nodiscard]] std::expected< Frame, std::string > readFrameBlocking( int sock, std::chrono::milliseconds timeout );

} // namespace idhan::ipc
