#include "ClusterAccess.hpp"

#include <boost/uuid/uuid_io.hpp>

#include <fcntl.h>
#include <format>

#include "ClusterMarker.hpp"

namespace idhan::filesystem
{

ClusterAccess::ClusterAccess( folly::File root, const boost::uuids::uuid uuid, const bool readonly ) :
  m_root( std::move( root ) ),
  m_uuid( uuid ),
  m_readonly( readonly )
{}

std::expected< ClusterAccess, std::string > ClusterAccess::open(
	const std::filesystem::path& root,
	const boost::uuids::uuid& expected,
	const bool readonly )
{
	auto file { folly::File::makeFile( root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC ) };
	if ( !file ) return std::unexpected( std::format( "Cannot open cluster directory {}", root.string() ) );

	ClusterAccess access { std::move( *file ), expected, readonly };
	auto valid { access.verify() };
	if ( !valid ) return std::unexpected( std::move( valid.error() ) );

	return access;
}

std::expected< void, std::string > ClusterAccess::verify() const
{
	const auto marker { readClusterMarker( m_root.fd() ) };
	if ( !marker ) return std::unexpected( marker.error() );

	if ( !*marker )
	{
		if ( m_readonly ) return {};

		return std::unexpected( "Cluster marker is missing; restart with the acknowledged marker-recovery flag" );
	}

	if ( ( **marker ).uuid != m_uuid )
		return std::unexpected(
			std::format( "Cluster marker UUID does not match expected {}", boost::uuids::to_string( m_uuid ) ) );
	return {};
}

std::filesystem::path ClusterAccess::root() const
{
	return std::format( "/proc/self/fd/{}", m_root.fd() );
}

} // namespace idhan::filesystem
