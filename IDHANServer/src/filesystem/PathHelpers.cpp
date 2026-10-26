#include "PathHelpers.hpp"

#include <format>

#include "idhan/SHA256.hpp"

namespace idhan::filesystem
{
std::filesystem::path getClusterRelativePath( const SHA256& sha256, std::string_view extension )
{
	if ( extension.starts_with( '.' ) ) extension.remove_prefix( 1 );
	const auto hex { sha256.hex() };
	const auto filename { extension.empty() ? hex : std::format( "{}.{}", hex, extension ) };
	return std::filesystem::path { std::format( "f{}", hex.substr( 0, 2 ) ) } / filename;
}
} // namespace idhan::filesystem
