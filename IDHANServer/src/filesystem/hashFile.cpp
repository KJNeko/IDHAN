#include "hashFile.hpp"

#include <algorithm>
#include <format>
#include <span>
#include <stdexcept>

#include "idhan/SHA256Hasher.hpp"
#include "io/IOUring.hpp"

namespace idhan::filesystem
{

runtime::Task< SHA256 > hashFile( std::filesystem::path path )
{
	FileIOUring file { path };
	SHA256Hasher hasher;
	constexpr std::size_t chunk_size { 1024 * 1024 };
	std::size_t offset {};
	while ( offset < file.size() )
	{
		const auto chunk { co_await file.read( offset, std::min( chunk_size, file.size() - offset ) ) };
		if ( chunk.empty() ) throw std::runtime_error( std::format( "Short read while hashing {}", path.string() ) );

		hasher.update( std::span< const std::byte > { chunk } );
		offset += chunk.size();
	}
	co_return hasher.finish();
}

} // namespace idhan::filesystem
