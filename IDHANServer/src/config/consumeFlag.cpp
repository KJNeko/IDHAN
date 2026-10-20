#include "consumeFlag.hpp"

#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <folly/File.h>
#include <folly/FileUtil.h>
#include <sys/stat.h>

#include <cctype>
#include <fcntl.h>
#include <format>
#include <unistd.h>

#include "Config.hpp"

namespace idhan::config
{

static std::size_t sourceOffset( const std::string& text, const toml::source_position position )
{
	std::size_t offset {};
	for ( std::size_t line { 1 }; line < position.line; ++line )
	{
		const auto end { text.find( '\n', offset ) };
		if ( end == std::string::npos ) throw std::runtime_error( "Invalid config source position" );
		offset = end + 1;
	}
	for ( std::size_t column { 1 }; column < position.column; ++column )
	{
		if ( offset >= text.size() ) throw std::runtime_error( "Invalid config source column" );
		++offset;
		while ( offset < text.size() && ( static_cast< unsigned char >( text[ offset ] ) & 0xc0 ) == 0x80 ) ++offset;
	}
	return offset;
}

static std::expected< void, std::string > resetFileFlag(
	const std::filesystem::path& path,
	const std::string_view group,
	const std::string_view name )
{
	auto original { folly::File::makeFile( path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW ) };
	if ( !original ) return std::unexpected( "Cannot open recovery configuration for resetting" );

	std::string text;
	if ( !folly::readFile( original->fd(), text ) ) return std::unexpected( "Cannot read recovery configuration" );

	auto document { toml::parse( text ) };
	const auto node { document.at_path( group )[ name ] };
	if ( !node.is_boolean() || !node.value< bool >().value_or( false ) )
		return std::unexpected( "Recovery acknowledgement changed before it could be reset" );
	const auto begin { sourceOffset( text, node.node()->source().begin ) };
	const auto end { sourceOffset( text, node.node()->source().end ) };
	if ( text.substr( begin, end - begin ) != "true" )
		return std::unexpected( "Recovery acknowledgement must be a TOML boolean" );
	text.replace( begin, end - begin, "false" );
	struct stat status {};
	if ( ::fstat( original->fd(), &status ) != 0 ) return std::unexpected( "Cannot inspect recovery configuration" );

	const auto parent { path.has_parent_path() ? path.parent_path() : std::filesystem::path { "." } };
	auto directory { folly::File::makeFile( parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC ) };
	if ( !directory ) return std::unexpected( "Cannot open recovery configuration directory" );

	const auto temporary { ".idhan-config-" + boost::uuids::to_string( boost::uuids::random_generator {}() ) + ".tmp" };
	const int descriptor {
		::openat( directory->fd(), temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600 )
	};
	if ( descriptor < 0 ) return std::unexpected( "Cannot create a temporary recovery configuration" );

	folly::File output { descriptor, true };
	const bool written {
		folly::writeFull( output.fd(), text.data(), text.size() ) == static_cast< ssize_t >( text.size() )
	};
	const bool permissions { ::fchmod( output.fd(), status.st_mode & 0777 ) == 0 };
	const bool owner { ::fchown( output.fd(), status.st_uid, status.st_gid ) == 0 };
	if ( !written || !permissions || !owner || ::fsync( output.fd() ) != 0 )
	{
		::unlinkat( directory->fd(), temporary.c_str(), 0 );
		return std::unexpected( "Cannot persist reset recovery acknowledgement" );
	}
	struct stat current {};
	if ( ::fstatat( directory->fd(), path.filename().c_str(), &current, AT_SYMLINK_NOFOLLOW ) != 0
	     || current.st_dev != status.st_dev || current.st_ino != status.st_ino
	     || current.st_mtim.tv_sec != status.st_mtim.tv_sec || current.st_mtim.tv_nsec != status.st_mtim.tv_nsec
	     || current.st_size != status.st_size )
	{
		::unlinkat( directory->fd(), temporary.c_str(), 0 );
		return std::unexpected( "Recovery configuration was concurrently modified; refusing to overwrite it" );
	}
	if ( ::renameat( directory->fd(), temporary.c_str(), directory->fd(), path.filename().c_str() ) != 0 )
	{
		::unlinkat( directory->fd(), temporary.c_str(), 0 );
		return std::unexpected( "Cannot replace recovery configuration with reset acknowledgement" );
	}
	if ( ::fsync( directory->fd() ) != 0 ) return std::unexpected( "Cannot synchronize reset recovery configuration" );

	return {};
}

std::expected< bool, std::string > consumeFlag( const std::string_view group, const std::string_view name )
{
	const auto cli { tryGetCLI< bool >( group, name ) };
	if ( cli )
	{
		if ( *cli )
			return std::unexpected(
				"Self-resetting recovery flags must be set in a writable TOML file, not CLI options" );
		return false;
	}
	const auto environment { tryGetEnv< bool >( group, name ) };
	if ( environment )
	{
		if ( *environment )
			return std::unexpected(
				"Self-resetting recovery flags must be set in a writable TOML file, not environment variables" );
		return false;
	}
	std::vector< std::filesystem::path > candidates;
	const auto explicit_path { getUserConfigPath() };
	if ( !explicit_path.empty() )
		candidates.emplace_back( expand_home( explicit_path ) );
	else
		for ( const auto path : config_paths | std::views::reverse ) candidates.emplace_back( expand_home( path ) );
	try
	{
		for ( const auto& path : candidates )
		{
			if ( !std::filesystem::exists( path ) ) continue;
			const auto document { toml::parse_file( path.string() ) };
			const auto node { document.at_path( group )[ name ] };
			if ( !node ) continue;
			if ( !node.is_boolean() ) return std::unexpected( "Recovery acknowledgement must be a TOML boolean" );

			if ( !node.value< bool >().value_or( false ) ) return false;

			auto reset { resetFileFlag( path, group, name ) };
			if ( !reset ) return std::unexpected( std::move( reset.error() ) );

			return true;
		}
	}
	catch ( const std::exception& error )
	{
		return std::unexpected( std::format( "Cannot consume recovery acknowledgement: {}", error.what() ) );
	}
	return false;
}

} // namespace idhan::config
