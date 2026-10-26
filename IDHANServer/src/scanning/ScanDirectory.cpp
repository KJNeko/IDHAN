#include "ScanDirectory.hpp"

#include <system_error>
#include <utility>

namespace idhan::scanning
{

ScanDirectory::ScanDirectory( std::filesystem::path root ) : m_root( std::move( root ) )
{
	std::error_code error {};
	m_folders = std::filesystem::directory_iterator { m_root, error };
	if ( error ) m_failure = ScanFailure { m_root, error.message() };
}

std::expected< std::optional< ScanCandidate >, ScanFailure > ScanDirectory::next()
{
	if ( m_failure )
	{
		auto failure { std::move( *m_failure ) };
		m_failure.reset();
		return std::unexpected( std::move( failure ) );
	}

	const std::filesystem::directory_iterator end {};
	while ( true )
	{
		std::error_code error {};
		if ( m_advance_file )
		{
			m_advance_file = false;
			m_files.increment( error );
			if ( error )
			{
				m_files = end;
				return std::unexpected( ScanFailure { m_folders->path(), error.message() } );
			}
		}

		if ( m_files != end )
		{
			const auto entry { *m_files };
			m_advance_file = true;
			if ( entry.path().extension() == ".thumbnail" ) continue;

			const auto status { entry.symlink_status( error ) };
			if ( error ) return std::unexpected( ScanFailure { entry.path(), error.message() } );

			if ( !std::filesystem::is_regular_file( status ) ) continue;

			const auto size { entry.file_size( error ) };
			if ( error ) return std::unexpected( ScanFailure { entry.path(), error.message() } );

			return ScanCandidate { entry.path(), size };
		}

		if ( m_advance_folder )
		{
			m_advance_folder = false;
			m_folders.increment( error );
			if ( error )
			{
				m_folders = end;
				return std::unexpected( ScanFailure { m_root, error.message() } );
			}
		}

		if ( m_folders == end ) return std::nullopt;

		const auto folder { *m_folders };
		m_advance_folder = true;
		if ( folder.path().filename() == "bad" ) continue;

		const auto status { folder.symlink_status( error ) };
		if ( error ) return std::unexpected( ScanFailure { folder.path(), error.message() } );

		if ( !std::filesystem::is_directory( status ) ) continue;

		m_files = std::filesystem::directory_iterator { folder.path(), error };
		if ( error ) return std::unexpected( ScanFailure { folder.path(), error.message() } );
	}
}

} // namespace idhan::scanning
