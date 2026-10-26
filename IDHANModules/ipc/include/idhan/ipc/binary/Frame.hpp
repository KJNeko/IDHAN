#pragma once
#include <folly/File.h>

#include <expected>
#include <memory>
#include <span>
#include <vector>

#include "Messages.hpp"

namespace idhan::ipc::binary
{
struct Frame
{
	Header header;
	Message message;
	std::vector< folly::File > descriptors;
};

class FrameCodec
{
  public:

	[[nodiscard]] static std::expected< std::vector< std::byte >, ProtocolError > encode( const Frame& );
	[[nodiscard]] static std::expected< Frame, ProtocolError > decode(
		std::span< const std::byte >,
		std::vector< folly::File >,
		std::uint32_t max_payload );
};

class FrameReader
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	explicit FrameReader( std::uint32_t max_payload );
	~FrameReader();
	[[nodiscard]] std::expected< std::vector< Frame >, ProtocolError > read( int socket );
	[[nodiscard]] bool eof() const noexcept;
};

class FrameWriter
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	explicit FrameWriter( std::uint32_t max_payload );
	~FrameWriter();
	[[nodiscard]] std::expected< void, ProtocolError > enqueue( Frame );
	[[nodiscard]] std::expected< bool, ProtocolError > flush( int socket );
	[[nodiscard]] bool empty() const noexcept;
};
} // namespace idhan::ipc::binary
