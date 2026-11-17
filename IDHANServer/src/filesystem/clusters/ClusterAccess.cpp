#include "ClusterAccess.hpp"

#include <fcntl.h>
#include <format>
#include <system_error>

#include "ClusterMarker.hpp"
#include "filesystem/io/IOUring.hpp"

namespace idhan::filesystem
{

runtime::Task< std::expected< ClusterAccess, std::string > > ClusterAccess::open(
	const std::filesystem::path root,
	const boost::uuids::uuid expected,
	const bool readonly )
{
	const int descriptor { co_await IOUring::getInstance().openFile( root, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0 ) };
	if ( descriptor < 0 )
		co_return std::unexpected(
			std::format(
				"Cannot open cluster directory {}: {}",
				root.string(),
				std::error_code { -descriptor, std::generic_category() }.message() ) );

	ClusterAccess access { folly::File { descriptor, true }, expected, readonly };
	const auto valid_e { co_await access.verify() };
	if ( !valid_e.has_value() )
	{
		(void)co_await access.close();
		co_return std::unexpected( valid_e.error() );
	}

	co_return access;
}

runtime::Task< std::expected< void, std::string > > ClusterAccess::verify() const
{
	const auto marker_e { co_await readClusterMarker( m_root.fd() ) };
	if ( !marker_e.has_value() ) co_return std::unexpected( marker_e.error() );

	co_return verifyMarker( marker_e.value() );
}

runtime::Task< int > ClusterAccess::close()
{
	const int descriptor { m_root.release() };
	if ( descriptor < 0 ) co_return 0;

	co_return co_await IOUring::getInstance().closeFile( static_cast< IOUring::NativeHandle >( descriptor ) );
}

} // namespace idhan::filesystem
