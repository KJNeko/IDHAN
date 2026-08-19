#pragma once

#include <cstddef>
#include <string_view>

#include "hydrus/HydrusConstants_gen.hpp"

namespace idhan::hyapi
{

//! Mirrors Hydrus' `service_string_lookup` for the service types IDHAN reports.
//! \return The Hydrus label for \p type, or "null service" for a type IDHAN does not report.
constexpr std::string_view servicePrettyName( const std::size_t type )
{
	switch ( type )
	{
		case hydrus::gen_constants::TAG_REPOSITORY:
			return "hydrus tag repository";
		case hydrus::gen_constants::FILE_REPOSITORY:
			return "hydrus file repository";
		case hydrus::gen_constants::LOCAL_FILE_DOMAIN:
			return "local file domain";
		case hydrus::gen_constants::LOCAL_TAG:
			return "local tag domain";
		case hydrus::gen_constants::COMBINED_TAG:
			return "virtual combined tag domain";
		case hydrus::gen_constants::COMBINED_FILE:
			return "virtual combined file domain";
		case hydrus::gen_constants::LOCAL_FILE_TRASH_DOMAIN:
			return "local trash file domain";
		case hydrus::gen_constants::HYDRUS_LOCAL_FILE_STORAGE:
			return "virtual combined local file domain";
		case hydrus::gen_constants::LOCAL_FILE_UPDATE_DOMAIN:
			return "local update file domain";
		case hydrus::gen_constants::COMBINED_LOCAL_FILE_DOMAINS:
			return "virtual combined local media domain";
		default:
			return "null service";
	}
}

} // namespace idhan::hyapi
