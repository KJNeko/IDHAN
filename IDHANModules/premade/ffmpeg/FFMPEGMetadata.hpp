//
// Created by kj16609 on 11/12/25.
//
#pragma once
#include "MetadataModule.hpp"

//! Metadata parser for video/audio containers via FFmpeg (dimensions, duration, bitrate, fps, audio).
class FFMPEGMetadata final : public idhan::MetadataModuleI
{
  public:

	FFMPEGMetadata( idhan::ModuleCallbacks callbacks ) : MetadataModuleI( callbacks ) {}

	[[nodiscard]] std::string_view name() override;

	[[nodiscard]] idhan::ModuleVersion version() override;

	// each call owns its AVFormatContext/codec contexts, no shared state: safe to run concurrently
	[[nodiscard]] bool threadSafe() override { return true; }

	[[nodiscard]] std::vector< std::string_view > handleableMimes() override;

	[[nodiscard]] std::expected< idhan::MetadataInfo, idhan::ModuleError > parseFile( idhan::ModuleCallData& data )
		override;
};
