#include "DownloadImports.hpp"

#include <IDHANDownloader/DirectResponse.hpp>
#include <folly/coro/BlockingWait.h>

#include <format>
#include <span>
#include <utility>
#include <vector>

#include "db/fileInfo.hpp"
#include "filesystem/recordFilePath.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "import/ImportFile.hpp"
#include "tags/AssociateTags.hpp"
#include "urls/AssociateUrls.hpp"

namespace idhan::downloader
{

[[nodiscard]] static std::vector< std::string > collectUrls( const Json::Value& options, std::string url )
{
	std::vector< std::string > urls { std::move( url ) };

	if ( const auto& associated { options[ "urls" ] }; associated.isArray() )
	{
		for ( const auto& entry : associated )
		{
			if ( entry.isObject() && entry[ "url" ].isString() ) urls.emplace_back( entry[ "url" ].asString() );
		}
	}

	if ( const auto& discovered { options[ "discoveredUrls" ] }; discovered.isArray() )
	{
		for ( const auto& entry : discovered )
		{
			if ( entry.isString() ) urls.emplace_back( entry.asString() );
		}
	}

	return urls;
}

[[nodiscard]] static std::vector< std::string > collectTags( const Json::Value& options )
{
	std::vector< std::string > tags {};

	if ( const auto& declared { options[ "tags" ] }; declared.isArray() )
	{
		for ( const auto& tag : declared )
		{
			if ( tag.isString() ) tags.emplace_back( tag.asString() );
		}
	}

	return tags;
}

static runtime::Task< std::expected< ImportResult, std::string > > finishImport(
	const RecordID record_id,
	const bool existing,
	std::vector< std::string > urls,
	std::vector< std::string > tags,
	const std::string tag_domain,
	const DownloadSessionID session_id,
	db::DBContext& db )
{
	if ( const auto result { co_await helpers::associateUrls( record_id, std::move( urls ), db ) }; !result )
		co_return std::unexpected( std::format( "Associating URLs with record {}: {}", record_id, result.error() ) );

	if ( const auto result { co_await helpers::associateTags( record_id, std::move( tags ), tag_domain, db ) };
	     !result )
		co_return std::unexpected( std::format( "Applying tags to record {}: {}", record_id, result.error() ) );

	std::string note { existing ? "Already in the database" : "" };

	if ( session_id != 0 )
	{
		db::DBBindings bindings {};
		bindings.bind( session_id );
		bindings.bind( record_id );
		const auto recorded {
			co_await db
				.query(
					"INSERT INTO download_session_records (download_session_id, record_id) "
					"VALUES ($1, $2) ON CONFLICT DO NOTHING RETURNING record_id",
					std::move( bindings ) )
				.rows< RecordID >()
		};

		const bool first_in_session { !recorded.empty() };

		if ( existing )
			note = first_in_session ? "Already in the database" : "Already imported earlier in this session";
	}

	co_return ImportResult { .record_id = record_id, .note = std::move( note ) };
}

//! A free function rather than a capturing lambda: a lazy coroutine reads its captures after the
//! closure that owned them has gone, so all state travels as parameters.
[[nodiscard]] static runtime::Task< std::expected< ImportResult, std::string > > runImport(
	const std::span< const std::byte > data,
	const SHA256 sha256,
	const std::string filename,
	const std::string description,
	std::vector< std::string > direct_urls,
	std::vector< std::string > urls,
	std::vector< std::string > tags,
	const std::string tag_domain,
	const DownloadSessionID session_id,
	db::DBContext& db )
{
	auto imported { co_await imports::importFile( data, sha256, filename, false, db ) };
	if ( !imported )
		co_return std::unexpected( std::format( "Importing {}: {}", description, imported.error().message ) );

	if ( imported->status == Failed )
		co_return std::unexpected( std::format( "Downloaded file has an unrecognised MIME type: {}", description ) );

	if ( imported->status == Deleted )
		co_return std::unexpected( std::format( "Downloaded file was previously deleted: {}", description ) );

	auto result { co_await finishImport(
		imported->record_id,
		imported->status == Exists,
		std::move( urls ),
		std::move( tags ),
		tag_domain,
		session_id,
		db ) };
	if ( !result ) co_return std::unexpected( result.error() );

	// Only the URL that delivered bytes and its final redirect destination are direct.
	co_await db::markDirectUrls( db, std::move( direct_urls ) );
	co_return *result;
}

static runtime::Task< std::expected< std::optional< ImportResult >, std::string > > reuseImport(
	ImportRequest request,
	std::string tag_domain,
	db::DBContext& db )
{
	const auto record { co_await db::findStoredDirectRecordByUrl( db, request.url, request.ephemeral_query ) };
	if ( !record ) co_return std::nullopt;

	const auto present { co_await filesystem::findPresentRecordFilePath( db, *record ) };
	if ( !present ) co_return std::unexpected( present.error() );

	if ( !*present ) co_return std::nullopt;

	const auto result { co_await finishImport(
		*record,
		true,
		collectUrls( request.options, request.url ),
		collectTags( request.options ),
		std::move( tag_domain ),
		static_cast< DownloadSessionID >( request.host_tag ),
		db ) };
	if ( !result ) co_return std::unexpected( result.error() );

	co_await db::markDirectUrls( db, { request.url } );
	co_return *result;
}

std::expected< std::optional< ImportResult >, std::string > DownloadImports::reuse( const ImportRequest& request )
{
	return folly::coro::blockingWait( reuseImport( request, m_tag_domain, db::DBContext::instance() ) );
}

std::expected< ImportResult, std::string > DownloadImports::Sink::finish( const ImportMetadata& metadata )
{
	const auto size { metadata.size };
	auto mapping { m_buffer.map( size ) };
	if ( !mapping )
	{
		m_buffer.discard();
		return std::unexpected( mapping.error() );
	}

	const std::span< const std::byte > data { mapping->get(), size };
	auto urls { collectUrls( m_options, m_url ) };
	if ( !metadata.final_url.empty() ) urls.emplace_back( metadata.final_url );
	const std::string filename { m_filename.empty() ? metadata.filename : m_filename };
	const std::string description { std::format(
		"{} ({} bytes{})",
		m_url,
		size,
		metadata.content_type.empty() ? std::string {} : std::format( ", {}", metadata.content_type ) ) };

	auto outcome { folly::coro::blockingWait( runImport(
		data,
		SHA256::hash( data ),
		filename,
		description,
		m_direct_eligible
		        && isDirectResponse(
					metadata.content_type,
					std::string_view { reinterpret_cast< const char* >( data.data() ),
					                   std::min< std::size_t >( data.size(), 512 ) } ) ?
			std::vector< std::string > { m_url, metadata.final_url } :
			std::vector< std::string > {},
		std::move( urls ),
		collectTags( m_options ),
		m_tag_domain,
		m_session_id,
		db::DBContext::instance() ) ) };

	m_buffer.discard();
	return std::move( outcome );
}

} // namespace idhan::downloader
