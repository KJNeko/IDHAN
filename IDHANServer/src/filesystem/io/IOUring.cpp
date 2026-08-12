// Platform-independent FileIOUring members. Everything here is written in terms of read() and
// size(), both of which every backend provides, so it does not belong in linux/ or windows/.

#include "filesystem/io/IOUring.hpp"

#include <algorithm>
#include <cstring>

namespace idhan
{

drogon::Task< std::vector< std::byte > > FileIOUring::readAll() const
{
	// io_uring's SQE length field is a __u32, so one op can never cover 4 GiB or more. 512 MiB keeps
	// every realistic file to a single op while staying well clear of that ceiling.
	constexpr std::size_t block_size { 512ull * 1024ull * 1024ull };

	// Single-op fast path. read() hands its buffer back by move, so nothing is copied and peak
	// memory is exactly the file size. The chunked path below cannot make that claim.
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

		// A chunk shorter than asked for means EOF or the file shrank underneath us. The rest of the
		// preallocated buffer was never written and must not be handed out as file content.
		if ( chunk.size() < want ) break;
	}

	buffer.resize( total );

	co_return std::move( buffer );
}

} // namespace idhan
