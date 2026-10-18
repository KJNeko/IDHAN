#include "renameNoReplace.hpp"

#ifdef __linux__
#include <cerrno>
#include <fcntl.h>
#include <stdio.h>
#endif

namespace idhan::filesystem
{

std::expected< void, std::error_code > renameNoReplace(
	const std::filesystem::path& from,
	const std::filesystem::path& to )
{
#ifdef __linux__
	if ( ::renameat2( AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE ) != 0 )
		return std::unexpected( std::error_code { errno, std::generic_category() } );

	return {};
#else
	return std::unexpected( std::make_error_code( std::errc::operation_not_supported ) );
#endif
}

} // namespace idhan::filesystem
