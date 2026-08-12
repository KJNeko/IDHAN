#include "ipc/BlobFile.hpp"

#include <algorithm>
#include <cstring>

namespace idhan::ipc
{

std::expected< std::size_t, ModuleError > BlobFile::read( const std::span< std::byte > out, const std::size_t offset )
	const
{
	const auto bytes { m_blob.bytes() };

	// Past the end reads nothing rather than failing, matching RingFile. Modules must not need a
	// branch per backend.
	if ( out.empty() || offset >= bytes.size() ) return 0;

	const std::size_t count { std::min( bytes.size() - offset, out.size() ) };

	std::memcpy( out.data(), bytes.data() + offset, count );

	return count;
}

} // namespace idhan::ipc
