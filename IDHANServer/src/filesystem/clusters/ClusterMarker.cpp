#include "ClusterMarker.hpp"

#include <folly/File.h>

#include <cerrno>
#include <fcntl.h>
#include <format>
#include <system_error>

#include "filesystem/io/IOUring.hpp"

namespace idhan::filesystem
{

static runtime::Task< std::expected< std::optional< ClusterMarker >, std::string > > readOpenedClusterMarker(
	IOUring& io,
	const int descriptor )
{
	const auto status_e { co_await io.fileStatus( std::format( "/proc/self/fd/{}", descriptor ) ) };
	if ( !status_e.has_value() )
		co_return std::unexpected(
			std::format(
				"Cannot inspect cluster marker: {}",
				std::error_code { -status_e.error(), std::generic_category() }.message() ) );

	if ( !std::filesystem::is_regular_file( status_e.value() ) )
		co_return std::unexpected( "Cluster marker is not a regular file" );

	std::string text;
	while ( true )
	{
		const auto bytes { co_await io.read( static_cast< IOUring::NativeHandle >( descriptor ), text.size(), 4096 ) };
		if ( bytes.empty() ) break;

		text.append( reinterpret_cast< const char* >( bytes.data() ), bytes.size() );
	}

	const auto marker_e { parseClusterMarker( text ) };
	if ( !marker_e.has_value() ) co_return std::unexpected( marker_e.error() );

	co_return marker_e.value();
}

runtime::Task< std::expected< std::optional< ClusterMarker >, std::string > > readClusterMarker( const int root_fd )
{
	IOUring& io { IOUring::getInstance() };
	const int descriptor { co_await io.openFile(
		std::filesystem::path { std::format( "/proc/self/fd/{}", root_fd ) } / clusterMarkerFilename,
		O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK,
		0 ) };
	if ( descriptor == -ENOENT ) co_return std::optional< ClusterMarker > {};

	if ( descriptor < 0 )
		co_return std::unexpected(
			std::format(
				"Cannot open cluster marker: {}",
				std::error_code { -descriptor, std::generic_category() }.message() ) );

	folly::File file { descriptor, true };
	std::expected< std::optional< ClusterMarker >, std::string > marker_e;
	try
	{
		marker_e = co_await readOpenedClusterMarker( io, descriptor );
	}
	catch ( const std::exception& error )
	{
		marker_e = std::unexpected( std::format( "Cannot read cluster marker: {}", error.what() ) );
	}

	const int closed { co_await io.closeFile( static_cast< IOUring::NativeHandle >( file.release() ) ) };
	if ( !marker_e.has_value() ) co_return std::unexpected( marker_e.error() );

	if ( closed < 0 )
		co_return std::unexpected(
			std::format(
				"Cannot close cluster marker: {}", std::error_code { -closed, std::generic_category() }.message() ) );

	co_return marker_e;
}

} // namespace idhan::filesystem
