#include "ModuleFile.hpp"

#include <algorithm>
#include <cstring>

namespace idhan
{

//! A ModuleFile over memory somebody else owns.
class MemoryFile final : public ModuleFile
{
	std::span< const std::byte > m_bytes;

  public:

	explicit MemoryFile( const std::span< const std::byte > bytes ) : m_bytes( bytes ) {}

	[[nodiscard]] std::size_t size() const override { return m_bytes.size(); }

	[[nodiscard]] std::span< const std::byte > mapped() const override { return m_bytes; }

	[[nodiscard]] std::expected< std::size_t, ModuleError > read(
		const std::span< std::byte > out,
		const std::size_t offset ) const override
	{
		if ( offset >= m_bytes.size() ) return 0;

		const std::size_t available { m_bytes.size() - offset };
		const std::size_t count { std::min( available, out.size() ) };

		if ( count > 0 ) std::memcpy( out.data(), m_bytes.data() + offset, count );

		return count;
	}
};

std::unique_ptr< ModuleFile > ModuleFile::fromBytes( const std::span< const std::byte > bytes )
{
	return std::make_unique< MemoryFile >( bytes );
}

} // namespace idhan
