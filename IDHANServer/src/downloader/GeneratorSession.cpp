#include "GeneratorSession.hpp"

#include <json/writer.h>

#include <ada.h>
#include <stdexcept>

#include "idhan/db/DBContext.hpp"
#include "idhan/db/DBTransaction.hpp"

namespace idhan::downloader
{
static std::pair< std::string, std::string > generatorSettings( const GeneratorRequest& request )
{
	const auto separator { request.generator.rfind( ':' ) };
	if ( separator == std::string::npos || separator == 0 || separator + 1 == request.generator.size() )
		throw std::invalid_argument( "Invalid generator: " + request.generator );

	const auto parser { request.generator.substr( 0, separator ) };
	Json::Value settings { Json::objectValue };
	settings[ "required_parser" ] = parser;
	settings[ "generator" ][ "parser" ] = parser;
	settings[ "generator" ][ "handler" ] = request.generator.substr( separator + 1 );
	settings[ "generator" ][ "origin" ] = request.origin;
	settings[ "generator" ][ "parameters" ] = Json::Value { Json::objectValue };
	ada::url_search_params query;
	for ( const auto& [ key, value ] : request.parameters )
	{
		settings[ "generator" ][ "parameters" ][ key ] = value;
		query.append( key, value );
	}
	const auto url {
		( request.origin.empty() ? "https://subscription.invalid/" : request.origin + "/" ) + "?" + query.to_string()
	};
	return { url, Json::writeString( Json::StreamWriterBuilder {}, settings ) };
}

runtime::Task< DownloadSessionID > createGeneratorSession(
	db::DBTransaction& transaction,
	const SubscriptionID subscription_id,
	const GeneratorRequest& request )
{
	const auto [ url, settings ] { generatorSettings( request ) };

	db::DBBindings bindings;
	bindings.bind( subscription_id );
	bindings.bind( settings );
	const auto rows {
		co_await transaction
			.query(
				//language=postgresql
				R"SQL(
			INSERT INTO download_sessions (name, options, is_subscription, subscription_id)
			VALUES ('Subscription ' || $1::text || ' check ' || gen_random_uuid()::text, $2::jsonb, true, $1)
			RETURNING download_session_id
		)SQL",
				std::move( bindings ) )
			.rows< DownloadSessionID >()
	};
	const auto session_id { std::get< 0 >( rows.row( 0 ) ) };
	db::DBBindings root;
	root.bind( session_id );
	root.bind( url );
	co_await transaction
		.query(
			//language=postgresql
			R"SQL(
			INSERT INTO download_session_urls (download_session_id, download_session_url_id, url)
			VALUES ($1, 1, $2)
		)SQL",
			std::move( root ) )
		.result();
	co_return session_id;
}

runtime::Task<> configureGeneratorSession(
	db::DBTransaction& transaction,
	const DownloadSessionID session_id,
	const GeneratorRequest& request )
{
	const auto [ url, settings ] { generatorSettings( request ) };
	db::DBBindings bindings;
	bindings.bind( session_id );
	bindings.bind( settings );
	co_await transaction
		.query(
			"UPDATE download_sessions SET options = $2::jsonb WHERE download_session_id = $1", std::move( bindings ) )
		.result();
	// Clear the previous completion in the same transaction that activates the next check.
	// A crash before submission must leave an interrupted check, never a stale successful result.
	db::DBBindings previous_result;
	previous_result.bind( session_id );
	co_await transaction
		.query( "DELETE FROM download_session_results WHERE download_session_id = $1", std::move( previous_result ) )
		.result();

	db::DBBindings roots;
	roots.bind( session_id );
	roots.bind( url );
	co_await transaction
		.query(
			//language=postgresql
			R"SQL(
			UPDATE download_session_urls SET url = $2
			WHERE download_session_id = $1 AND parent_url_id IS NULL
		)SQL",
			std::move( roots ) )
		.result();
}

runtime::Task< std::vector< DownloadSessionID > > subscriptionSessionIds( db::DBContext& db )
{
	const auto rows { co_await db.query( "SELECT download_session_id FROM download_sessions WHERE is_subscription" )
	                      .rows< DownloadSessionID >() };
	std::vector< DownloadSessionID > ids;
	for ( const auto& [ id ] : rows ) ids.push_back( id );
	co_return ids;
}

runtime::Task< std::vector< RecordID > > generatorRecords( db::DBContext& db, const DownloadSessionID session_id )
{
	db::DBBindings bindings;
	bindings.bind( session_id );
	const auto rows {
		co_await db
			.query(
				//language=postgresql
				R"SQL(
			SELECT record_id FROM download_session_records
			WHERE download_session_id = $1 ORDER BY record_id
		)SQL",
				std::move( bindings ) )
			.rows< RecordID >()
	};
	std::vector< RecordID > ids;
	for ( const auto& [ id ] : rows ) ids.push_back( id );
	co_return ids;
}
} // namespace idhan::downloader
