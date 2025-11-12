//
// Created by kj16609 on 4/3/25.
//
#pragma once

#include "hydrus/HydrusConstants_gen.hpp"
#include "mime_type_map.hpp"

namespace idhan::hydrus::hy_constants
{

enum ServiceTypes
{
	PTR_SERVICE = gen_constants::TAG_REPOSITORY,
	TAG_SERVICE = gen_constants::LOCAL_TAG
};

inline std::uint16_t simpleToHyType( SimpleMimeType type )
{
	switch ( type )
	{
		default:
			[[fallthrough]];
		case SimpleMimeType::NONE:
			[[fallthrough]];
		case SimpleMimeType::IMAGE:
			return gen_constants::GENERAL_IMAGE;
		case SimpleMimeType::VIDEO:
			return gen_constants::GENERAL_VIDEO;
		case SimpleMimeType::ANIMATION:
			return gen_constants::GENERAL_ANIMATION;
		case SimpleMimeType::AUDIO:
			return gen_constants::GENERAL_AUDIO;
		case SimpleMimeType::ARCHIVE:
			return gen_constants::GENERAL_APPLICATION_ARCHIVE;
		case SimpleMimeType::IMAGE_PROJECT:
			return gen_constants::GENERAL_IMAGE_PROJECT;
	}
}

inline std::uint16_t mimeToHyType( const std::string& mime_name )
{
	if ( auto itter = hy_type_mime.find( mime_name ); itter != hy_type_mime.end() ) return itter->second;

	return gen_constants::GENERAL_IMAGE;
}

} // namespace idhan::hydrus::hy_constants