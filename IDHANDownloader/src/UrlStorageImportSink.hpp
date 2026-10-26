#pragma once

#include <IDHANDownloader/ImportSink.hpp>

#include "scripts/ScriptRegistry.hpp"

namespace idhan::downloader
{

// The context drains sessions and lane transfers before destroying its registry.
// Resolve redirect destinations with their own URL class, rather than the request's policy.
class UrlStorageImportSink final : public ImportSink
{
	std::unique_ptr< ImportSink > m_sink;
	const ScriptRegistry& m_registry;

  public:

	UrlStorageImportSink( std::unique_ptr< ImportSink > sink, const ScriptRegistry& registry ) :
	  m_sink( std::move( sink ) ),
	  m_registry( registry )
	{}

	std::expected< void, std::string > write( const std::span< const std::byte > bytes ) override
	{
		return m_sink->write( bytes );
	}

	std::expected< ImportResult, std::string > finish( const ImportMetadata& metadata ) override
	{
		auto stored { metadata };
		stored.final_url = m_registry.storageUrl( metadata.final_url );
		return m_sink->finish( stored );
	}

	void abort() override { m_sink->abort(); }
};

} // namespace idhan::downloader
