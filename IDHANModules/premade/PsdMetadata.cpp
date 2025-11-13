//
// Created by kj16609 on 11/12/25.
//
#include "PsdMetadata.hpp"

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

std::expected< idhan::MetadataInfo, idhan::ModuleError > PsdMetadata::parseFile(
	const void* data,
	std::size_t length,
	[[maybe_unused]] std::string mime_name )
{
	idhan::MetadataInfo generic_metadata {};
	idhan::MetadataInfoImageProject project_metadata {};

	generic_metadata.m_simple_type = idhan::SimpleMimeType::IMAGE_PROJECT;

	generic_metadata.m_metadata = project_metadata;

	return generic_metadata;
}

std::vector< std::string_view > PsdThumbnailer::handleableMimes()
{
	return { "application/psd" };
}

std::string_view PsdThumbnailer::name()
{
	return "PSD Thumbnailer Parser";
}

idhan::ModuleVersion PsdThumbnailer::version()
{
	return { .m_major = 1, .m_minor = 0, .m_patch = 0 };
}

std::expected< idhan::ThumbnailerModuleI::ThumbnailInfo, idhan::ModuleError > PsdThumbnailer::createThumbnail(
	const void* data,
	std::size_t length,
	std::size_t target_width,
	std::size_t target_height,
	[[maybe_unused]] std::string mime_name )
{
	ThumbnailInfo info {};

	return info;
}