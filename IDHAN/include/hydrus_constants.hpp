#pragma once

#include <hydrus/HydrusConstants_gen.hpp>
#include "mime_type_map.hpp"

namespace idhan::hydrus::hy_constants
{

enum ServiceTypes
{
	PTR_SERVICE = gen_constants::TAG_REPOSITORY,
	TAG_SERVICE = gen_constants::LOCAL_TAG
};

inline std::uint16_t mimeToHyType( const std::string& mime_name )
{
	const auto itter { hy_type_mime.find( mime_name ) };
	if ( itter != hy_type_mime.end() ) return static_cast< std::uint16_t >( itter->second );

	return gen_constants::GENERAL_IMAGE;
}

} // namespace idhan::hydrus::hy_constants
