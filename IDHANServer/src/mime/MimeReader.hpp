#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "threading/IDHANTask.hpp"

namespace idhan
{
class FileIOUring;
}

namespace idhan::mime
{

constexpr std::size_t min_read_size { 32 * 1024 };

class MimeReader
{
	std::variant< std::shared_ptr< FileIOUring >, std::span< const std::byte > > m_source;

	mutable std::vector< std::byte > m_window {};
	mutable std::size_t m_window_offset { 0 };

	[[nodiscard]] IDHANTask< std::span< const std::byte > > at( std::size_t offset, std::size_t length ) const;

  public:

	explicit MimeReader( std::shared_ptr< FileIOUring > file );
	explicit MimeReader( std::span< const std::byte > data );
	explicit MimeReader( std::span< const std::uint8_t > data );
	explicit MimeReader( std::string_view data );

	[[nodiscard]] std::size_t size() const;

	[[nodiscard]] IDHANTask< bool > matchAt( std::size_t offset, std::string_view pattern ) const;

	[[nodiscard]] IDHANTask< std::optional< std::size_t > > find(
		std::string_view pattern,
		std::size_t from,
		std::size_t limit ) const;
};

} // namespace idhan::mime
