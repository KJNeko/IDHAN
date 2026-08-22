#include "PsdMetadata.hpp"

#include "MimeIDs.hpp"

#include <algorithm>
#include <cstring>
#include <memory>

#include "ModuleBase.hpp"
#include "psd.hpp"

using namespace psd;

std::vector< idhan::MimeID > PsdMetadata::handleableMimes()
{
	return { idhan::mime_ids::APPLICATION_PSD };
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
	const auto contents { readWholeFile( data.file ) };
	if ( !contents ) return std::unexpected( contents.error() );

	const auto* bytes { contents->data() };
	const auto length { contents->size() };

	const auto header { parsePSDHeader( bytes, length ) };
	if ( !header )
	{
		return std::unexpected( idhan::ModuleError { "Invalid PSD header" } );
	}

	idhan::MetadataInfo generic_metadata {};
	idhan::MetadataInfoImageProject project_metadata {};

	project_metadata.image_info.width = static_cast< int >( header->width );
	project_metadata.image_info.height = static_cast< int >( header->height );
	project_metadata.image_info.channels = static_cast< std::uint8_t >( header->channels );
	project_metadata.layers = static_cast< std::uint8_t >( countPSDLayers( bytes, length ) );

	generic_metadata.m_simple_type = idhan::SimpleMimeType::IMAGE_PROJECT;
	generic_metadata.m_metadata = project_metadata;

	return generic_metadata;
}