#include "DownloadImports.hpp"

#include <drogon/drogon.h>

#include "import/ImportFile.hpp"
#include "logging/format_ns.hpp"
#include "tags/tags.hpp"
#include "urls/urls.hpp"

namespace idhan::downloader
{

static std::string downloaderResponseError( const std::string_view operation, const drogon::HttpResponsePtr& response )
{
	if ( response == nullptr ) return format_ns::format( "{} failed for an unreported reason", operation );

	return format_ns::format(
		"{} failed with HTTP {}: {}",
		operation,
		static_cast< int >( response->statusCode() ),
		response->body().substr( 0, 2048 ) );
}

std::expected< ImportResult, std::string > DownloadImports::Sink::finish( const ImportMetadata& metadata )
{
	const auto size { metadata.size };
	auto mapping { m_buffer.map( size ) };
	if ( !mapping ) return std::unexpected( mapping.error() );

	const std::span< const std::byte > data { mapping->get(), size };
	const std::string filename { m_filename.empty() ? metadata.filename : m_filename };
	const std::string description { format_ns::format(
		"{} ({} bytes{})",
		m_url,
		size,
		metadata.content_type.empty() ? std::string {} : format_ns::format( ", {}", metadata.content_type ) ) };

	auto completed = [ & ]() -> drogon::Task< std::expected< ImportResult, std::string > >
	{
		const auto db { drogon::app().getDbClient() };
		const auto imported { co_await imports::importFile( data, filename, false, db ) };

		if ( !imported )
			co_return std::unexpected(
				downloaderResponseError( format_ns::format( "Importing {}", description ), imported.error() ) );

		if ( imported->status == ImportStatus::Failed )
			co_return std::unexpected(
				format_ns::format( "Downloaded file has an unrecognised MIME type: {}", description ) );

		if ( imported->status == ImportStatus::Deleted )
			co_return std::unexpected( format_ns::format( "Downloaded file was previously deleted: {}", description ) );

		std::vector< std::string > urls { m_url };

		if ( const auto& associated { m_options[ "urls" ] }; associated.isArray() )
		{
			for ( const auto& entry : associated )
			{
				if ( entry.isObject() && entry[ "url" ].isString() ) urls.emplace_back( entry[ "url" ].asString() );
			}
		}

		if ( const auto& discovered { m_options[ "discoveredUrls" ] }; discovered.isArray() )
		{
			for ( const auto& url : discovered )
			{
				if ( url.isString() ) urls.emplace_back( url.asString() );
			}
		}

		if ( const auto result { co_await helpers::associateUrls( imported->record_id, std::move( urls ), db ) };
		     !result )
			co_return std::unexpected( downloaderResponseError(
				format_ns::format( "Associating URLs with record {}", imported->record_id ), result.error() ) );

		std::vector< std::string > tags {};

		if ( const auto& declared { m_options[ "tags" ] }; declared.isArray() )
		{
			for ( const auto& tag : declared )
			{
				if ( tag.isString() ) tags.emplace_back( tag.asString() );
			}
		}

		if ( const auto result { co_await associateTags( imported->record_id, tags, m_tag_domain, db ) }; !result )
			co_return std::unexpected( downloaderResponseError(
				format_ns::format( "Applying tags to record {}", imported->record_id ), result.error() ) );

		std::string note {};

		if ( m_session_id != 0 )
		{
			const auto recorded { co_await db->execSqlCoro(
				"INSERT INTO download_session_records (download_session_id, record_id) VALUES ($1, $2) "
				"ON CONFLICT DO NOTHING RETURNING record_id",
				m_session_id,
				imported->record_id ) };

			const bool first_in_session { !recorded.empty() };

			if ( imported->status == ImportStatus::Exists )
				note = first_in_session ? "Already in the database" : "Already imported earlier in this session";
		}

		co_return ImportResult { .record_id = imported->record_id, .note = std::move( note ) };
	};

	auto outcome { drogon::sync_wait( completed() ) };

	m_buffer.discard();
	return outcome;
}

} // namespace idhan::downloader
