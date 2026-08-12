#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <system_error>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "filesystem.hpp"
#include "logging/log.hpp"
#include "paths.hpp"

namespace idhan::filesystem
{

std::expected< std::size_t, std::string > clearThumbnailCache()
{
	const auto path { getThumbnailsPath() };

	std::error_code error {};

	if ( !std::filesystem::exists( path, error ) )
	{
		if ( error ) return std::unexpected( std::format( "{}: {}", path.string(), error.message() ) );

		log::debug( "Thumbnail cache {} does not exist, nothing to purge", path.string() );
		return 0;
	}

	// Counted before the delete rather than by removing entries one at a time: remove_all does the
	// whole tree in one call. The count is only ever reported, never acted on, so an entry that fails
	// to stat is not a reason to abandon the purge -- it just goes uncounted.
	std::size_t files { 0 };
	std::error_code count_error {};
	for ( const auto& entry : std::filesystem::recursive_directory_iterator( path, count_error ) )
	{
		std::error_code entry_error {};
		if ( entry.is_regular_file( entry_error ) ) ++files;
	}

	std::filesystem::remove_all( path, error );
	if ( error ) return std::unexpected( std::format( "{}: {}", path.string(), error.message() ) );

	// Recreated immediately: everything downstream assumes the cache root exists and only ever creates
	// the per-prefix subdirectory under it.
	std::filesystem::create_directories( path, error );
	if ( error ) return std::unexpected( std::format( "{}: {}", path.string(), error.message() ) );

	return files;
}

} // namespace idhan::filesystem
