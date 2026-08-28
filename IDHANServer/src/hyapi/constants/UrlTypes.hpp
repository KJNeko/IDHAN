#pragma once

#include <cstddef>
#include <string_view>

#include "hydrus/HydrusConstants_gen.hpp"

namespace idhan::hyapi
{

//! Mirrors Hydrus' `url_type_string_lookup`.
//! \return The Hydrus label for \p type, or "unknown url" for a type Hydrus does not name.
constexpr std::string_view urlTypeString( const std::size_t type )
{
	switch ( type )
	{
		case hydrus::gen_constants::URL_TYPE_POST:
			return "post url";
		case hydrus::gen_constants::URL_TYPE_API:
			return "api/redirect url";
		case hydrus::gen_constants::URL_TYPE_FILE:
			return "file url";
		case hydrus::gen_constants::URL_TYPE_GALLERY:
			return "gallery url";
		case hydrus::gen_constants::URL_TYPE_WATCHABLE:
			return "watchable url";
		case hydrus::gen_constants::URL_TYPE_NEXT:
			return "next page url";
		case hydrus::gen_constants::URL_TYPE_DESIRED:
			return "downloadable/pursuable url";
		case hydrus::gen_constants::URL_TYPE_SUB_GALLERY:
			return "sub-gallery url (is queued even if creator found no post/file urls)";
		default:
			return "unknown url";
	}
}

} // namespace idhan::hyapi
