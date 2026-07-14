//
// Created by kj16609 on 4/3/25.
//
#pragma once

#include "hydrus/HydrusConstants_gen.hpp"
#include "mime_type_map.hpp"

namespace idhan::hydrus::hy_constants
{

//! Hydrus service-type values IDHAN presents for its tag services.
enum ServiceTypes
{
	PTR_SERVICE = gen_constants::TAG_REPOSITORY, //!< A public tag repository (PTR).
	TAG_SERVICE = gen_constants::LOCAL_TAG       //!< A local tag service.
};

//! Maps a canonical MIME string to its Hydrus mime constant.
//! \return The matching Hydrus constant, or GENERAL_IMAGE if \p mime_name is unknown.
inline std::uint16_t mimeToHyType( const std::string& mime_name )
{
	if ( auto itter = hy_type_mime.find( mime_name ); itter != hy_type_mime.end() ) return itter->second;

	return gen_constants::GENERAL_IMAGE;
}

} // namespace idhan::hydrus::hy_constants