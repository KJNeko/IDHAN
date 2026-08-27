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

	// avformat_find_stream_info decodes to probe, so it reaches the decoder's threads too
	[[nodiscard]] bool singleThreaded() override { return false; }

	// shares a worker with FFMPEGThumbnailer, whose vips init is what makes the process worth keeping
	[[nodiscard]] idhan::ModuleResidency residency() override { return idhan::ModuleResidency::PERSISTENT; }

	[[nodiscard]] std::vector< idhan::MimeID > handleableMimes() override;

	[[nodiscard]] std::expected< idhan::MetadataInfo, idhan::ModuleError > parseFile( idhan::ModuleCallData& data )
		override;
};
