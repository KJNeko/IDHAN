
#include "filesystem/io/IOUring.hpp"

#include <algorithm>
#include <cstring>

namespace idhan
{

drogon::Task< std::vector< std::byte > > FileIOUring::readAll() const
{
	constexpr std::size_t block_size { 512ull * 1024ull * 1024ull };

	if ( m_size <= block_size ) co_return co_await read( 0, m_size );

	std::vector< std::byte > buffer {};
	buffer.resize( m_size );

	std::size_t total { 0 };

	while ( total < m_size )
	{
		const std::size_t want { std::min( block_size, m_size - total ) };
		const auto chunk { co_await read( total, want ) };

		std::memcpy( buffer.data() + total, chunk.data(), chunk.size() );
		total += chunk.size();

		if ( chunk.size() < want ) break;
	}

	buffer.resize( total );

	co_return std::move( buffer );
}

} // namespace idhan
