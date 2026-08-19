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

	std::size_t files { 0 };
	for ( const auto& subfolder : std::filesystem::directory_iterator( path, error ) )
	{
		if ( !subfolder.is_directory() ) continue;
		const auto subfolder_name { subfolder.path().filename().string() };
		const auto starts_with_expected { subfolder_name.starts_with( 't' ) };
		const auto is_correct_len { subfolder_name.size() == 3 /* 'f00' */ };

		if ( !starts_with_expected || !is_correct_len )
		{
			log::warn( "Thumbnail directory had unknown folder {}, Ignoring it", subfolder_name );
			continue;
		}

		std::filesystem::remove_all( subfolder, error );
		if ( error ) return std::unexpected( std::format( "{}: {}", subfolder.path().string(), error.message() ) );
		std::filesystem::create_directories( subfolder );
	}

	return files;
}

} // namespace idhan::filesystem
