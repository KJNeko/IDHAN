#include "DownloadImports.hpp"

#include <format>
#include <map>
#include <span>
#include <utility>
#include <vector>

#include "db/fileInfo.hpp"
#include "db/downloadSessionRecords.hpp"
#include "filesystem/recordFilePath.hpp"
#include "idhan/SHA256.hpp"
#include "idhan/db/DBContext.hpp"
#include "import/ImportFile.hpp"
#include "logging/log.hpp"
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

[[nodiscard]] static std::map< std::string, std::vector< std::string > > collectTags( const Json::Value& options )
{
	std::map< std::string, std::vector< std::string > > tags {};

	const auto& declared { options[ "tags" ] };
	if ( declared.isArray() )
	{
		for ( const auto& tag : declared )
		{
			if ( tag.isString() ) tags[ "downloader tags" ].emplace_back( tag.asString() );
		}
	}

	if ( declared.isObject() )
	{
		for ( const auto& domain : declared.getMemberNames() )
		{
			const auto& entries { declared[ domain ] };
			if ( !entries.isArray() ) continue;

			for ( const auto& tag : entries )
			{
				if ( tag.isString() ) tags[ domain ].emplace_back( tag.asString() );
			}
		}
	}

	return tags;
}

static runtime::Task< std::expected< ImportResult, std::string > > finishImport(
	const RecordID record_id,
	const bool existing,
	std::vector< std::string > urls,
	std::map< std::string, std::vector< std::string > > tags_by_domain,
	const DownloadSessionID session_id,
	const std::optional< std::int64_t > posted_at,
	db::DBContext& db )
{
	if ( const auto result { co_await helpers::associateUrls( record_id, std::move( urls ), db ) }; !result )
		co_return std::unexpected( std::format( "Associating URLs with record {}: {}", record_id, result.error() ) );

	for ( auto& [ domain, tags ] : tags_by_domain )
	{
		const auto result_e { co_await helpers::associateTags( record_id, std::move( tags ), domain, db ) };
		if ( !result_e.has_value() )
			co_return std::unexpected(
				std::format( "Applying tags to record {} in domain '{}': {}", record_id, domain, result_e.error() ) );
	}

	std::string note { existing ? "Already in the database" : "" };

	if ( session_id != 0 )
	{
		const bool first_in_session {
			co_await db::recordDownloadSessionImport( db, session_id, record_id, posted_at )
		};

		if ( existing )
			note = first_in_session ? "Already in the database" : "Already imported earlier in this session";
	}

	co_return ImportResult { .record_id = record_id, .note = std::move( note ), .existing = existing };
}

//! A free function rather than a capturing lambda: a lazy coroutine reads its captures after the
//! closure that owned them has gone, so all state travels as parameters.
[[nodiscard]] static runtime::Task< std::expected< ImportResult, std::string > > runImport(
	const std::span< const std::byte > data,
	const std::optional< int > descriptor,
	const SHA256 sha256,
	const std::string filename,
	const std::string description,
	const bool force_download,
	db::DBContext& db )
{
	auto imported_e { co_await imports::importFile( data, descriptor, sha256, filename, force_download, db ) };
	if ( !imported_e.has_value() )
		co_return std::unexpected( std::format( "Importing {}: {}", description, imported_e.error().message ) );

	const auto& imported { imported_e.value() };
	if ( imported.status == Failed )
		co_return std::unexpected( std::format( "Downloaded file has an unrecognised MIME type: {}", description ) );

	if ( imported.status == Deleted )
		co_return std::unexpected( std::format( "Downloaded file was previously deleted: {}", description ) );

	co_return ImportResult { .record_id = imported.record_id, .existing = imported.status == Exists };
}

folly::coro::Task< std::expected< std::optional< RecordID >, std::string > > DownloadImports::findStoredRecord(
	ImportRequest request )
{
	if ( request.force_download )
	{
		log::info( "Stored file check for {}: downloading, force download is set", request.url );
		co_return std::nullopt;
	}

	auto& db { db::DBContext::instance() };
	const auto record { co_await db::findStoredDirectRecordByUrl( db, request.url ) };
	if ( !record.has_value() )
	{
		log::info( "Stored file check for {}: downloading, no single stored record has it as a file URL", request.url );
		co_return std::nullopt;
	}

	auto present_e { co_await filesystem::findPresentRecordFilePath( db, record.value() ) };
	if ( !present_e.has_value() ) co_return std::unexpected( present_e.error() );

	auto& present { present_e.value() };
	if ( !present.has_value() )
	{
		log::info(
			"Stored file check for {}: downloading, record {} is missing its file", request.url, record.value() );
		co_return std::nullopt;
	}

	const int closed { co_await present.value().close() };
	if ( closed < 0 ) co_return std::unexpected( "Cannot close stored record's cluster directory" );

	log::info( "Stored file check for {}: skipping download, reusing record {}", request.url, record.value() );
	co_return record;
}

folly::coro::Task< std::expected< ImportResult, std::string > > DownloadImports::Sink::finish( ImportMetadata metadata )
{
	ImportResult imported {};
	std::vector< std::string > direct_urls {};
	if ( m_stored_record.has_value() )
	{
		imported = ImportResult { .record_id = m_stored_record.value(), .existing = true };
		direct_urls.emplace_back( m_url );
	}
	else
	{
		const auto size { metadata.size };
		auto data_e { co_await m_buffer.read( size ) };
		if ( !data_e.has_value() )
		{
			co_await m_buffer.discard();
			co_return std::unexpected( data_e.error() );
		}

		const auto& data { data_e.value() };

		if ( !metadata.effective_url.empty() ) direct_urls = { m_url, metadata.effective_url };

		const std::string filename { m_filename.empty() ? metadata.filename : m_filename };
		const std::string description { std::format(
			"{} ({} bytes{})",
			m_url,
			size,
			metadata.content_type.empty() ? std::string {} : std::format( ", {}", metadata.content_type ) ) };
		auto imported_e { co_await runImport(
			data,
			m_buffer.descriptor(),
			m_hasher.finish(),
			filename,
			description,
			m_force_download,
			db::DBContext::instance() ) };
		co_await m_buffer.discard();
		if ( !imported_e.has_value() ) co_return std::unexpected( imported_e.error() );

		imported = imported_e.value();
	}

	auto urls { collectUrls( m_options, m_url ) };
	if ( !metadata.effective_url.empty() ) urls.emplace_back( metadata.effective_url );

	auto& db { db::DBContext::instance() };
	const auto posted_at {
		m_options[ "postedAt" ].isNull() ?
			std::nullopt :
			std::optional< std::int64_t > { m_options[ "postedAt" ].asInt64() }
	};
	auto result_e { co_await finishImport(
		imported.record_id,
		imported.existing,
		std::move( urls ),
		collectTags( m_options ),
		m_session_id,
		posted_at,
		db ) };
	if ( !result_e.has_value() ) co_return std::unexpected( result_e.error() );

	co_await db::markDirectUrls( db, std::move( direct_urls ) );
	co_return result_e.value();
}

} // namespace idhan::downloader
