#include <expected>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>
#include <system_error>

#include "IDHANTypes.hpp"
#include "db/dbTypes.hpp"
#include "filesystem.hpp"
#include "logging/log.hpp"
#include "paths.hpp"

namespace idhan::filesystem
{

constexpr std::string_view HEX_DIGITS { "0123456789abcdef" };

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

	std::size_t files { 0 };
	for ( const auto& subfolder : std::filesystem::directory_iterator( path, error ) )
	{
		if ( !subfolder.is_directory() ) continue;
		const auto subfolder_name { subfolder.path().filename().string() };
		const auto starts_with_expected { subfolder_name.starts_with( 't' ) };
		const auto is_correct_len { subfolder_name.size() == 3 /* 'f00' */ };

		if ( !starts_with_expected || !is_correct_len )
		{
			log::warn(
				"Thumbnail cache {} holds a folder named '{}', which is not a t00-tff bucket. Leaving it alone",
				path.string(),
				subfolder_name );
			continue;
		}

		const auto removed { std::filesystem::remove_all( subfolder, error ) };
		if ( error ) return std::unexpected( std::format( "{}: {}", subfolder.path().string(), error.message() ) );

		// remove_all counts the directory itself; the caller is told how many thumbnails went.
		if ( removed > 0 ) files += removed - 1;

		std::filesystem::create_directories( subfolder );
	}

	return files;
}

} // namespace idhan::filesystem
