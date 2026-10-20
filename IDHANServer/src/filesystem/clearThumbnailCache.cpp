#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <string_view>
#include <system_error>
#include <unistd.h>

#include "ThumbnailCache.hpp"

namespace idhan::filesystem
{

constexpr std::string_view HEX_DIGITS { "0123456789abcdef" };

struct CloseThumbnailDirectory
{
	void operator()( DIR* directory ) const noexcept { ::closedir( directory ); }
};

using ThumbnailDirectory = std::unique_ptr< DIR, CloseThumbnailDirectory >;

static std::expected< ThumbnailDirectory, std::string > thumbnailDirectory( const int descriptor )
{
	if ( descriptor < 0 )
		return std::unexpected(
			std::system_error( errno, std::generic_category(), "Opening thumbnail directory" ).what() );

	auto* directory { ::fdopendir( descriptor ) };
	if ( !directory )
	{
		const auto error { errno };
		::close( descriptor );
		return std::unexpected(
			std::system_error( error, std::generic_category(), "Reading thumbnail directory" ).what() );
	}

	return ThumbnailDirectory { directory };
}

static bool isThumbnailBucket( const std::string_view name )
{
	if ( name.size() != 3 ) return false;

	return name.front() == 't' && HEX_DIGITS.contains( name[ 1 ] ) && HEX_DIGITS.contains( name[ 2 ] );
}

static bool isThumbnailFilename( const std::string_view bucket, const std::string_view name )
{
	if ( name.size() < 71 ) return false;

	if ( !name.ends_with( ".webp" ) || name[ 64 ] != '.' ) return false;

	const auto hash { name.substr( 0, 64 ) };
	if ( !std::ranges::all_of( hash, []( const char digit ) { return HEX_DIGITS.contains( digit ); } ) ) return false;

	if ( hash.substr( 0, 2 ) != bucket.substr( 1 ) ) return false;

	const auto size_text { name.substr( 65, name.size() - 70 ) };
	if ( size_text.front() == '0' ) return false;

	unsigned size {};
	const auto result { std::from_chars( size_text.data(), size_text.data() + size_text.size(), size ) };
	return result.ec == std::errc {} && result.ptr == size_text.data() + size_text.size() && size >= 1 && size <= 4096;
}

static std::expected< std::size_t, std::string > purgeThumbnailBucket( DIR* root, const char* bucket_name )
{
	auto bucket {
		thumbnailDirectory( ::openat( ::dirfd( root ), bucket_name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC ) )
	};
	if ( !bucket ) return std::unexpected( std::move( bucket.error() ) );

	std::size_t deleted {};
	while ( true )
	{
		errno = 0;
		const auto* entry { ::readdir( bucket->get() ) };
		if ( !entry )
		{
			if ( errno != 0 ) return std::unexpected( std::system_error( errno, std::generic_category() ).what() );
			return deleted;
		}

		if ( !isThumbnailFilename( bucket_name, entry->d_name ) ) continue;

		struct stat status {};
		if ( ::fstatat( ::dirfd( bucket->get() ), entry->d_name, &status, AT_SYMLINK_NOFOLLOW ) != 0 )
		{
			if ( errno == ENOENT ) continue;
			return std::unexpected( std::system_error( errno, std::generic_category() ).what() );
		}

		if ( !S_ISREG( status.st_mode ) ) continue;

		if ( ::unlinkat( ::dirfd( bucket->get() ), entry->d_name, 0 ) != 0 )
		{
			if ( errno == ENOENT ) continue;
			return std::unexpected( std::system_error( errno, std::generic_category() ).what() );
		}

		++deleted;
	}
}

std::expected< std::size_t, std::string > clearThumbnailCache( const std::filesystem::path& path )
{
	const auto descriptor { ::open( path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC ) };
	if ( descriptor < 0 && errno == ENOENT ) return 0;

	auto root { thumbnailDirectory( descriptor ) };
	if ( !root ) return std::unexpected( std::move( root.error() ) );

	std::size_t deleted {};
	while ( true )
	{
		errno = 0;
		const auto* entry { ::readdir( root->get() ) };
		if ( !entry )
		{
			if ( errno != 0 ) return std::unexpected( std::system_error( errno, std::generic_category() ).what() );
			return deleted;
		}

		if ( !isThumbnailBucket( entry->d_name ) ) continue;

		struct stat status {};
		if ( ::fstatat( ::dirfd( root->get() ), entry->d_name, &status, AT_SYMLINK_NOFOLLOW ) != 0 )
		{
			if ( errno == ENOENT ) continue;
			return std::unexpected( std::system_error( errno, std::generic_category() ).what() );
		}

		if ( !S_ISDIR( status.st_mode ) ) continue;

		auto result { purgeThumbnailBucket( root->get(), entry->d_name ) };
		if ( !result ) return std::unexpected( std::move( result.error() ) );

		deleted += *result;
	}
}

} // namespace idhan::filesystem
