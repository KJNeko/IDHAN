//
// Created by kj16609 on 11/12/25.
//
#include "PsdMetadata.hpp"

#include <vips/vips8>

#include <algorithm>
#include <cstring>
#include <memory>

#include "ModuleBase.hpp"
#include "psd.hpp"

using namespace psd;

std::vector< std::string_view > PsdMetadata::handleableMimes()
{
	return { "application/psd" };
}

std::string_view PsdMetadata::name()
{
	return "PSD Metadata Parser";
}

idhan::ModuleVersion PsdMetadata::version()
{
	return { .m_major = 1, .m_minor = 0, .m_patch = 0 };
}

std::expected< idhan::MetadataInfo, idhan::ModuleError > PsdMetadata::parseFile( idhan::ModuleCallData& data )
{
	const auto& [ data_view, mime, extra ] = data;
	const auto* bytes { static_cast< const std::uint8_t* >( data_view.data() ) };

	const auto header { parsePSDHeader( bytes, data_view.size() ) };
	if ( !header )
	{
		return std::unexpected( idhan::ModuleError { "Invalid PSD header" } );
	}

	idhan::MetadataInfo generic_metadata {};
	idhan::MetadataInfoImageProject project_metadata {};

	project_metadata.image_info.width = static_cast< int >( header->width );
	project_metadata.image_info.height = static_cast< int >( header->height );
	project_metadata.image_info.channels = static_cast< std::uint8_t >( header->channels );
	project_metadata.layers = static_cast< std::uint8_t >( countPSDLayers( bytes, data_view.size() ) );

	generic_metadata.m_simple_type = idhan::SimpleMimeType::IMAGE_PROJECT;
	generic_metadata.m_metadata = project_metadata;

	return generic_metadata;
}