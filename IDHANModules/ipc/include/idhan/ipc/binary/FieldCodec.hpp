#pragma once
#include <expected>
#include <optional>
#include <span>
#include <vector>

#include "Protocol.hpp"

namespace idhan::ipc::binary
{
struct Field
{
	std::uint16_t tag;
	FieldKind kind;
	bool required;
	std::span< const std::byte > bytes;
};

class FieldReader
{
	std::span< const std::byte > m_bytes;
	std::size_t m_offset { 0 };

  public:

	explicit FieldReader( std::span< const std::byte > );
	[[nodiscard]] std::expected< std::optional< Field >, ProtocolError > next();
	template < typename T >
	[[nodiscard]] std::expected< T, ProtocolError > scalar( Field );
};

class FieldWriter
{
	std::vector< std::byte > m_bytes;

  public:

	void append( std::uint16_t tag, FieldKind, bool required, std::span< const std::byte > );
	template < typename T >
	void scalar( std::uint16_t tag, bool required, T );
	[[nodiscard]] std::vector< std::byte > finish() &&;
};
} // namespace idhan::ipc::binary
