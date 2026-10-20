#include "ClusterMarker.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <folly/File.h>
#include <folly/FileUtil.h>
#include <json/reader.h>
#include <json/writer.h>
#include <sys/stat.h>

#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <format>
#include <system_error>
#include <unistd.h>

namespace idhan::filesystem
{

static std::string markerError( const std::string_view operation )
{
	return std::format( "{}: {}", operation, std::error_code { errno, std::generic_category() }.message() );
}

std::expected< std::optional< ClusterMarker >, std::string > readClusterMarker( const int root_fd )
{
	const int descriptor {
		::openat( root_fd, clusterMarkerFilename.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK )
	};
	if ( descriptor < 0 )
	{
		if ( errno == ENOENT ) return std::optional< ClusterMarker > {};

		return std::unexpected( markerError( "Cannot open cluster marker" ) );
	}
	folly::File file { descriptor, true };
	struct stat status {};
	if ( ::fstat( file.fd(), &status ) != 0 ) return std::unexpected( markerError( "Cannot inspect cluster marker" ) );

	if ( !S_ISREG( status.st_mode ) ) return std::unexpected( "Cluster marker is not a regular file" );

	std::string text;
	if ( !folly::readFile( file.fd(), text ) ) return std::unexpected( markerError( "Cannot read cluster marker" ) );

	Json::CharReaderBuilder builder;
	builder[ "rejectDupKeys" ] = true;
	builder[ "failIfExtra" ] = true;
	builder[ "allowComments" ] = false;
	builder[ "collectComments" ] = false;
	const std::unique_ptr< Json::CharReader > reader { builder.newCharReader() };
	Json::Value document;
	std::string errors;
	if ( !reader->parse( text.data(), text.data() + text.size(), &document, &errors ) )
		return std::unexpected( "Cluster marker contains invalid JSON" );

	if ( !document.isObject() ) return std::unexpected( "Cluster marker must be an object" );

	if ( !document[ "format" ].isString() ) return std::unexpected( "Cluster marker format is missing" );

	if ( document[ "format" ].asString() != "idhan.cluster" )
		return std::unexpected( "Unrecognized cluster marker format" );
	if ( !document[ "version" ].isIntegral() ) return std::unexpected( "Cluster marker version must be an integer" );

	if ( document[ "version" ] != Json::Value { 1 } ) return std::unexpected( "Unsupported cluster marker version" );

	if ( !document[ "cluster_uuid" ].isString() ) return std::unexpected( "Cluster marker UUID is missing" );

	if ( !document[ "created_at" ].isUInt64() )
		return std::unexpected( "Cluster marker creation time must be nonnegative Unix seconds" );
	if ( !document[ "metadata" ].isObject() ) return std::unexpected( "Cluster marker metadata must be an object" );

	const auto uuid_text { document[ "cluster_uuid" ].asString() };
	if ( uuid_text.size() != 36 ) return std::unexpected( "Cluster marker UUID is invalid" );
	try
	{
		const auto uuid { boost::uuids::string_generator {}( uuid_text ) };
		if ( uuid.is_nil() ) return std::unexpected( "Cluster marker UUID must not be nil" );

		return ClusterMarker { uuid, std::move( document ) };
	}
	catch ( const std::exception& )
	{
		return std::unexpected( "Cluster marker UUID is invalid" );
	}
}

std::expected< ClusterMarker, std::string > initializeClusterMarker( const int root_fd, const boost::uuids::uuid& uuid )
{
	auto existing { readClusterMarker( root_fd ) };
	if ( !existing ) return std::unexpected( std::move( existing.error() ) );

	if ( *existing ) return std::move( **existing );

	if ( uuid.is_nil() ) return std::unexpected( "Cannot initialize a nil cluster UUID" );

	Json::Value document { Json::objectValue };
	document[ "format" ] = "idhan.cluster";
	document[ "version" ] = 1;
	document[ "cluster_uuid" ] = boost::uuids::to_string( uuid );
	document[ "created_at" ] = static_cast< Json::UInt64 >(
		std::chrono::duration_cast< std::chrono::seconds >( std::chrono::system_clock::now().time_since_epoch() )
			.count() );
	document[ "metadata" ] = Json::Value { Json::objectValue };
	Json::StreamWriterBuilder writer;
	writer[ "indentation" ] = "  ";
	const auto text { Json::writeString( writer, document ) + "\n" };
	const auto temporary {
		".idhan-cluster-" + boost::uuids::to_string( boost::uuids::random_generator {}() ) + ".tmp"
	};
	const int descriptor {
		::openat( root_fd, temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644 )
	};
	if ( descriptor < 0 ) return std::unexpected( markerError( "Cannot create cluster marker" ) );

	folly::File file { descriptor, true };
	if ( folly::writeFull( file.fd(), text.data(), text.size() ) != static_cast< ssize_t >( text.size() ) )
	{
		const auto error { markerError( "Cannot write cluster marker" ) };
		::unlinkat( root_fd, temporary.c_str(), 0 );
		return std::unexpected( error );
	}
	if ( ::fsync( file.fd() ) != 0 )
	{
		const auto error { markerError( "Cannot synchronize cluster marker" ) };
		::unlinkat( root_fd, temporary.c_str(), 0 );
		return std::unexpected( error );
	}
	const int installed { ::linkat( root_fd, temporary.c_str(), root_fd, clusterMarkerFilename.data(), 0 ) };
	const int install_error { errno };
	::unlinkat( root_fd, temporary.c_str(), 0 );
	if ( installed != 0 && install_error != EEXIST )
		return std::unexpected(
			std::format(
				"Cannot install cluster marker: {}",
				std::error_code { install_error, std::generic_category() }.message() ) );
	if ( ::fsync( root_fd ) != 0 ) return std::unexpected( markerError( "Cannot synchronize cluster directory" ) );
	existing = readClusterMarker( root_fd );
	if ( !existing ) return std::unexpected( std::move( existing.error() ) );

	if ( !*existing ) return std::unexpected( "Cluster marker disappeared during initialization" );

	return std::move( **existing );
}

} // namespace idhan::filesystem
