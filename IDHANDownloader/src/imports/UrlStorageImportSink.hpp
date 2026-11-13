#pragma once

#include <IDHANDownloader/imports/ImportSink.hpp>

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

	folly::coro::Task< std::expected< void, std::string > > write( std::span< const std::byte > bytes ) override
	{
		co_return co_await m_sink->write( bytes );
	}

	folly::coro::Task< std::expected< ImportResult, std::string > > finish( ImportMetadata metadata ) override
	{
		if ( !metadata.effective_url.empty() )
		{
			auto normalized_e { m_registry.normalizeUrl( metadata.effective_url ) };
			if ( !normalized_e.has_value() ) co_return std::unexpected( normalized_e.error() );

			metadata.effective_url = std::move( normalized_e.value().url );
		}

		co_return co_await m_sink->finish( std::move( metadata ) );
	}

	void abort() override { m_sink->abort(); }
};

} // namespace idhan::downloader
