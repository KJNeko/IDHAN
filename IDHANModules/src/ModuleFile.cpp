#include "ModuleFile.hpp"

#include <algorithm>
#include <cstring>

namespace idhan
{

namespace
{

//! A ModuleFile over memory somebody else owns.
/** Backs ModuleFile::fromBytes. The host's real backends (a restricted io_uring, a mapped memfd)
 *  live in the ipc layer, which this header cannot reach -- ipc depends on the module ABI, not the
 *  other way round. This one can live here because it depends on nothing at all. */
class MemoryFile final : public ModuleFile
{
	std::span< const std::byte > m_bytes;

  public:

	explicit MemoryFile( const std::span< const std::byte > bytes ) : m_bytes( bytes ) {}

	[[nodiscard]] std::size_t size() const override { return m_bytes.size(); }

	[[nodiscard]] std::expected< std::size_t, ModuleError > read(
		const std::span< std::byte > out,
		const std::size_t offset ) const override
	{
		// Past the end is not an error: a demuxer probing beyond the end of a file is ordinary
		// behaviour, and every backend has to agree on that or modules would need a branch per
		// backend.
		if ( offset >= m_bytes.size() ) return 0;

		const std::size_t available { m_bytes.size() - offset };
		const std::size_t count { std::min( available, out.size() ) };

		if ( count > 0 ) std::memcpy( out.data(), m_bytes.data() + offset, count );

		return count;
	}
};

} // namespace

std::unique_ptr< ModuleFile > ModuleFile::fromBytes( const std::span< const std::byte > bytes )
{
	return std::make_unique< MemoryFile >( bytes );
}

} // namespace idhan
