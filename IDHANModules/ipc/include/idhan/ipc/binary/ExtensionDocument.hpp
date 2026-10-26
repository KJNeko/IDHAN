#pragma once
#include <expected>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "Protocol.hpp"

namespace idhan::ipc::binary
{
enum class ExtensionKind : std::uint8_t
{
	null,
	boolean,
	i64,
	u64,
	f64,
	text,
	array,
	object
};

struct ExtensionNode
{
	ExtensionKind kind;
	std::span< const std::byte > payload;
};

class ExtensionDocument
{
	std::shared_ptr< const std::vector< std::byte > > m_bytes;
	explicit ExtensionDocument( std::shared_ptr< const std::vector< std::byte > > );

  public:

	[[nodiscard]] static std::expected< ExtensionDocument, ProtocolError > validate( std::vector< std::byte > );
	[[nodiscard]] std::span< const std::byte > bytes() const noexcept;
	// Nodes borrow this document's storage.
	[[nodiscard]] ExtensionNode root() const;
	[[nodiscard]] std::vector< ExtensionNode > elements( ExtensionNode ) const;
	[[nodiscard]] std::vector< std::pair< std::string_view, ExtensionNode > > members( ExtensionNode ) const;
};

class ExtensionBuilder
{
	struct Impl;
	std::unique_ptr< Impl > m_impl;

  public:

	ExtensionBuilder();
	~ExtensionBuilder();
	void null();
	void boolean( bool );
	void integer( std::int64_t );
	void unsignedInteger( std::uint64_t );
	void real( double );
	void text( std::string_view );
	void beginArray();
	void beginObject();
	void key( std::string_view );
	void end();
	[[nodiscard]] std::expected< ExtensionDocument, ProtocolError > finish();
};
} // namespace idhan::ipc::binary
