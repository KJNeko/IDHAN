#include "jobs.hpp"

#include <folly/ScopeGuard.h>
#include <folly/coro/Collect.h>
#include <folly/io/IOBuf.h>
#include <json/json.h>

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <simdjson.h>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "apiAuth.hpp"
#include "downloader/DownloadSessionManager.hpp"
#include "fileMetadata.hpp"
#include "idValidation.hpp"
#include "idhan/db/DBContext.hpp"
#include "jobs/JobContext.hpp"
#include "jobs/JobTask.hpp"
#include "jobs/UrlReorganizationProgress.hpp"
#include "jsonBody.hpp"
#include "logging/log.hpp"

namespace idhan::httpapi
{

[[nodiscard]] static http::PathParameter jobIdPathParameter()
{
	return http::PathParameter {
		"job_id",
		[]( const std::string_view value ) -> std::expected< void, http::ParseError >
		{
			idhan::JobID id {};
			const auto result { std::from_chars( value.data(), value.data() + value.size(), id ) };
			if ( result.ec != std::errc {} || result.ptr != value.data() + value.size() )
				return std::unexpected( http::ParseError { "job_id", "must be a job id" } );
			return {};
		}
	};
}

[[nodiscard]] static std::string responseText( const http::Response& response )
{
	const auto* body { std::get_if< std::unique_ptr< folly::IOBuf > >( &response.body ) };
	if ( !body || !*body ) return "Unknown error";

	return ( *body )->to< std::string >();
}

[[nodiscard]] static std::string toCompactJson( const Json::Value& value )
{
	Json::StreamWriterBuilder builder {};
	builder[ "indentation" ] = "";
	return Json::writeString( builder, value );
}

static void appendJobStatus(
	simdjson::builder::string_builder& builder,
	const idhan::JobID job_id,
	const std::shared_ptr< JobContext >& job,
	const bool cleanup_on_completion )
{
	if ( !job )
	{
		Json::Value missing;
		missing[ "job_id" ] = job_id;
		missing[ "status" ] = "not_found";
		builder.append_raw( toCompactJson( missing ) );
		return;
	}

	const auto status { job->status() };
	const auto snapshot { status->snapshot() };
	if ( cleanup_on_completion && snapshot[ "completed" ].asBool() ) status->m_cleanup_requested = true;
	builder.append_raw( toCompactJson( snapshot ) );
}

[[nodiscard]] static http::Response jsonResponse( simdjson::builder::string_builder& builder )
{
	auto json { builder.view() };
	if ( json.error() ) throw std::runtime_error( simdjson::error_message( json.error() ) );

	return http::Response::json( 200, folly::IOBuf::copyBuffer( *json ) );
}

static http::HTTPTask< http::Response > jobStatus( [[maybe_unused]] db::DBContext& db, http::RequestContext context )
{
	const auto job_id_e { context.parameters().scalar< idhan::JobID >( "job_id" ) };
	if ( !job_id_e ) co_return http::Response::error( 400, job_id_e.error().message );

	const auto job { getJobRuntime().getJob( *job_id_e ) };
	if ( !job ) co_return http::Response::error( 404, std::format( "Job {} not found", *job_id_e ) );

	simdjson::builder::string_builder builder;
	appendJobStatus( builder, *job_id_e, job, true );
	co_return jsonResponse( builder );
}

static http::HTTPTask< http::Response > jobsStatus( [[maybe_unused]] db::DBContext& db, http::RequestContext context )
{
	const auto cleanup_e { context.parameters().optional< bool >( "cleanup" ) };
	if ( !cleanup_e ) co_return http::Response::error( 400, cleanup_e.error().message );
	const auto cleanup { cleanup_e->value_or( false ) };

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.escape_and_append_with_quotes( "jobs" );
	builder.append_colon();
	builder.start_array();

	bool first { true };
	for ( const auto& job : getJobRuntime().getAllJobs() )
	{
		if ( !first ) builder.append_comma();
		first = false;
		appendJobStatus( builder, job->id(), job, cleanup );
	}

	builder.end_array();
	builder.end_object();
	co_return jsonResponse( builder );
}

static JobTask rescanMetadataJob( std::vector< RecordID > record_ids, db::DBContext& db )
{
	const auto executor { getJobRuntime().executor() };

	if ( record_ids.empty() )
	{
		const auto rows { co_await folly::coro::co_withExecutor(
			executor, db.query( "SELECT record_id FROM file_info" ).rows< RecordID >() ) };
		record_ids.reserve( rows.size() );
		for ( const auto& [ record_id ] : rows ) record_ids.push_back( record_id );
	}

	std::size_t scanned { 0 };
	Json::Value failures { Json::arrayValue };

	for ( const auto record_id : record_ids )
	{
		auto parsed { co_await folly::coro::co_withExecutor( executor, parseAndStoreRecordMetadata( db, record_id ) ) };
		if ( parsed )
		{
			++scanned;
			continue;
		}

		Json::Value failure;
		failure[ "record_id" ] = record_id;
		failure[ "error" ] = responseText( parsed.error() );
		failures.append( std::move( failure ) );
	}

	log::info( "Finished scanning metadata for {} records ({} failed)", scanned, failures.size() );

	Json::Value result;
	result[ "scanned_count" ] = static_cast< Json::UInt64 >( scanned );
	result[ "failed_count" ] = static_cast< Json::UInt64 >( failures.size() );
	result[ "failures" ] = std::move( failures );

	co_await setJobResponse( std::move( result ) );
}

static http::HTTPTask< http::Response > rescanMetadata( db::DBContext& db, http::RequestContext context )
{
	std::vector< RecordID > record_ids {};

	const auto* raw_body { std::get_if< std::unique_ptr< folly::IOBuf > >( &context.body() ) };
	const bool has_body { !raw_body || ( *raw_body && ( *raw_body )->computeChainDataLength() > 0 ) };

	if ( has_body )
	{
		auto body_e { readValidatedJsonBody( context ) };
		if ( !body_e ) co_return std::move( body_e.error() );

		simdjson::dom::parser parser;
		simdjson::dom::array records_array {};
		try
		{
			const simdjson::dom::element root { parser.parse( body_e->data(), body_e->size() ) };
			if ( root[ "records" ].get( records_array ) == simdjson::SUCCESS )
			{
				record_ids.reserve( records_array.size() );
				for ( const simdjson::dom::element item : records_array )
				{
					auto id { parsePositiveId< RecordID >( item, "record_id" ) };
					if ( !id ) co_return std::move( id.error() );

					record_ids.push_back( *id );
				}
			}
		}
		catch ( const simdjson::simdjson_error& e )
		{
			co_return http::Response::error( 400, e.what() );
		}
	}

	const auto job { queueJob( rescanMetadataJob( std::move( record_ids ), db ), "Rescanning record metadata" ) };

	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "job_id", job->id() );
	builder.end_object();
	co_return jsonResponse( builder );
}

struct UrlReorganizationGroup
{
	std::vector< UrlID > ids {};
	UrlID preferred_type_id { std::numeric_limits< UrlID >::max() };
	std::string preferred_type { "unknown" };
};

struct UrlSurvivorPreparation
{
	UrlID survivor {};
	std::optional< UrlID > collision {};
};

// Reserve the lowest ID's preferred type before removing any members. This preserves
// the global type preference even if a later batch fails and the job is rerun.
static runtime::Task< UrlSurvivorPreparation > prepareUrlSurvivor(
	db::DBContext& db,
	const UrlID survivor,
	std::string url,
	UrlID preferred_id,
	std::string preferred_type )
{
	auto transaction { co_await db.transaction() };
	co_await transaction.query( "LOCK TABLE urls IN SHARE ROW EXCLUSIVE MODE" ).result();
	db::DBBindings bindings {};
	bindings.bind( survivor );
	bindings.bind( url );
	const auto rows {
		co_await transaction
			.query(
				"SELECT url_id, url_type::text FROM urls WHERE url_id = $1 OR url = $2 ORDER BY url_id FOR UPDATE",
				std::move( bindings ) )
			.rows< UrlID, std::string >()
	};
	std::optional< UrlID > collision {};
	bool found_survivor {};
	for ( const auto& [ id, type ] : rows )
	{
		if ( id == survivor )
			found_survivor = true;
		else
			collision = id;

		if ( type != "unknown" && id <= preferred_id )
		{
			preferred_id = id;
			preferred_type = type;
		}
	}

	if ( !found_survivor )
		throw std::runtime_error( "A URL survivor was removed during reorganization; rerun the job" );

	const auto winner { collision.has_value() ? std::min( survivor, collision.value() ) : survivor };
	db::DBBindings type_bindings {};
	type_bindings.bind( winner );
	type_bindings.bind( preferred_type );
	co_await transaction
		.query(
			"UPDATE urls SET url_type = $2::UrlType WHERE url_id = $1 AND url_type <> $2::UrlType",
			std::move( type_bindings ) )
		.result();
	co_await transaction.commit();
	co_return UrlSurvivorPreparation { winner, collision };
}

// Each transaction moves at most 1000 mappings. Parent row locks prevent new
// references to the loser while its last mappings and URL row are removed.
static runtime::Task<> mergeUrlInChunks(
	db::DBContext& db,
	const UrlID survivor,
	const UrlID loser,
	std::shared_ptr< jobs::UrlReorganizationProgress > progress )
{
	for ( ;; )
	{
		auto transaction { co_await db.transaction() };
		db::DBBindings locks {};
		locks.bind( std::vector< UrlID > { survivor, loser } );
		const auto held {
			co_await transaction
				.query(
					"SELECT url_id FROM urls WHERE url_id = ANY($1::integer[]) ORDER BY url_id FOR UPDATE",
					std::move( locks ) )
				.rows< UrlID >()
		};
		if ( held.size() != 2 ) throw std::runtime_error( "URL rows changed during merging; rerun the job" );

		db::DBBindings bindings {};
		bindings.bind( survivor );
		bindings.bind( loser );
		const auto moved {
			co_await transaction
				.query(
					R"SQL(
			WITH batch AS (
			    SELECT record_id FROM url_mappings WHERE url_id = $2 ORDER BY record_id LIMIT 1000
			), removed AS (
			    DELETE FROM url_mappings m USING batch b
			    WHERE m.url_id = $2 AND m.record_id = b.record_id RETURNING m.record_id
			), inserted AS (
			    INSERT INTO url_mappings (record_id, url_id)
			    SELECT record_id, $1 FROM removed ON CONFLICT DO NOTHING
			)
			SELECT count(*) FROM removed
		)SQL",
					std::move( bindings ) )
				.rows< std::int64_t >()
		};
		const auto moved_count { std::get< 0 >( moved.row( 0 ) ) };
		const bool finished { moved_count < 1000 };
		if ( finished )
		{
			// Losers are visited in ascending original ID order. Equal timestamps keep
			// the earlier log, so ties consistently prefer the lowest original ID.
			db::DBBindings log_bindings {};
			log_bindings.bind( survivor );
			log_bindings.bind( loser );
			co_await transaction
				.query(
					R"SQL(
				INSERT INTO url_log (url_id, log_time, last_code)
				SELECT $1, log_time, last_code FROM url_log WHERE url_id = $2
				ON CONFLICT (url_id) DO UPDATE
				SET log_time = EXCLUDED.log_time, last_code = EXCLUDED.last_code
				WHERE EXCLUDED.log_time > url_log.log_time
				   OR (url_log.log_time IS NULL AND EXCLUDED.log_time IS NOT NULL)
			)SQL",
					std::move( log_bindings ) )
				.result();
			db::DBBindings type_bindings {};
			type_bindings.bind( survivor );
			type_bindings.bind( loser );
			co_await transaction
				.query(
					R"SQL(
				UPDATE urls s SET url_type = l.url_type FROM urls l
				WHERE s.url_id = $1 AND l.url_id = $2 AND s.url_type = 'unknown' AND l.url_type <> 'unknown'
			)SQL",
					std::move( type_bindings ) )
				.result();
			db::DBBindings delete_log {};
			delete_log.bind( loser );
			co_await transaction.query( "DELETE FROM url_log WHERE url_id = $1", std::move( delete_log ) ).result();
			db::DBBindings delete_url {};
			delete_url.bind( loser );
			co_await transaction.query( "DELETE FROM urls WHERE url_id = $1", std::move( delete_url ) ).result();
		}

		co_await transaction.commit();
		progress->batchCommitted( finished ? 1 : 0, moved_count );
		if ( finished ) co_return;
	}
}

// A concurrent importer may create the canonical spelling between batches. Check
// under a short write lock, merging that row too before claiming the sorted URL.
static runtime::Task< std::optional< UrlID > > saveSortedUrl( db::DBContext& db, const UrlID survivor, std::string url )
{
	auto transaction { co_await db.transaction() };
	co_await transaction.query( "LOCK TABLE urls IN SHARE ROW EXCLUSIVE MODE" ).result();
	db::DBBindings lookup {};
	lookup.bind( url );
	lookup.bind( survivor );
	const auto collision {
		co_await transaction.query( "SELECT url_id FROM urls WHERE url = $1 AND url_id <> $2", std::move( lookup ) )
			.rows< UrlID >()
	};
	if ( !collision.empty() )
	{
		const auto id { std::get< 0 >( collision.row( 0 ) ) };
		co_await transaction.commit();
		co_return id;
	}

	db::DBBindings update {};
	update.bind( survivor );
	update.bind( url );
	co_await transaction.query( "UPDATE urls SET url = $2 WHERE url_id = $1 AND url <> $2", std::move( update ) )
		.result();
	co_await transaction.commit();
	co_return std::nullopt;
}

static runtime::Task< Json::Value > reorganizeUrls(
	db::DBContext& db,
	std::shared_ptr< jobs::UrlReorganizationProgress > progress )
{
	static std::atomic_bool active {};
	if ( active.exchange( true ) ) throw std::runtime_error( "URL reorganization is already running" );

	const auto release { folly::makeGuard( [] { active.store( false ); } ) };
	auto& downloader { downloader::downloadSessionManager() };
	const auto initialized_e { downloader.classStorageUrls( {} ) };
	if ( !initialized_e.has_value() ) throw std::runtime_error( initialized_e.error() );

	// Bound the scan to IDs present at startup; new unsorted imports can be handled
	// by a later run. Keyset pages avoid a full-table count or an OFFSET scan.
	const auto last { co_await db.query( "SELECT url_id FROM urls ORDER BY url_id DESC LIMIT 1" ).rows< UrlID >() };
	const UrlID upper { last.empty() ? 0 : std::get< 0 >( last.row( 0 ) ) };
	UrlID cursor {};
	std::map< std::string, UrlReorganizationGroup > groups {};
	std::uint64_t total {};
	std::uint64_t sorted {};
	std::uint64_t planned_merges {};
	progress->setPhase( "sorting", "Reading and sorting URLs in pages of 1000" );
	for ( ;; )
	{
		db::DBBindings bindings {};
		bindings.bind( cursor );
		bindings.bind( upper );
		const auto rows {
			co_await db
				.query(
					"SELECT url_id, url, url_type::text FROM urls WHERE url_id > $1 AND url_id <= $2 ORDER BY url_id LIMIT 1000",
					std::move( bindings ) )
				.rows< UrlID, std::string, std::string >()
		};
		if ( rows.empty() ) break;

		std::vector< std::string > urls {};
		urls.reserve( rows.size() );
		for ( const auto& [ id, url, type ] : rows ) urls.push_back( url );
		auto sorted_e { downloader.classStorageUrls( std::move( urls ) ) };
		if ( !sorted_e.has_value() ) throw std::runtime_error( sorted_e.error() );

		auto& normalized { sorted_e.value() };
		std::size_t index {};
		for ( const auto& [ id, url, type ] : rows )
		{
			auto& target { normalized[ index++ ] };
			cursor = id;
			++total;
			// Already sorted rows cannot collide with each other (urls.url is UNIQUE).
			// Find their canonical spelling by its URL index when processing changed rows.
			if ( target == url ) continue;

			++sorted;
			auto& group { groups[ target ] };
			if ( !group.ids.empty() ) ++planned_merges;

			group.ids.push_back( id );
			if ( type != "unknown" && id < group.preferred_type_id )
			{
				group.preferred_type_id = id;
				group.preferred_type = type;
			}
		}

		progress->scanned( total, sorted );
	}

	progress->planned( total, total - planned_merges, planned_merges );
	std::uint64_t merged {};
	for ( auto& [ url, group ] : groups )
	{
		if ( group.ids.size() == 1 )
		{
			progress->setPhase( "saving", "Saving sorted URLs in committed batches" );
			const auto collision { co_await saveSortedUrl( db, group.ids.front(), url ) };
			if ( !collision.has_value() ) continue;
		}

		progress->setPhase( "merging", "Merging URL groups; moving at most 1000 mappings per transaction" );
		const auto prepared {
			co_await prepareUrlSurvivor( db, group.ids.front(), url, group.preferred_type_id, group.preferred_type )
		};
		const auto survivor { prepared.survivor };
		const auto collision { prepared.collision };
		if ( collision.has_value() && std::ranges::find( group.ids, collision.value() ) == group.ids.end() )
		{
			group.ids.push_back( collision.value() );
			if ( collision.value() > upper ) ++total;

			++planned_merges;
			progress->planned( total, total - planned_merges, planned_merges );
		}

		std::ranges::sort( group.ids );
		for ( std::size_t index { 1 }; index < group.ids.size(); ++index )
		{
			co_await mergeUrlInChunks( db, survivor, group.ids[ index ], progress );
			++merged;
		}

		for ( ;; )
		{
			progress->setPhase( "saving", "Saving sorted URLs in committed batches" );
			const auto added { co_await saveSortedUrl( db, survivor, url ) };
			if ( !added.has_value() ) break;

			if ( added.value() < survivor )
				throw std::runtime_error( "A lower URL ID appeared during reorganization; rerun the job" );

			if ( added.value() > upper ) ++total;

			++planned_merges;
			progress->planned( total, total - planned_merges, planned_merges );
			co_await mergeUrlInChunks( db, survivor, added.value(), progress );
			++merged;
		}
	}

	progress->committed( merged );
	Json::Value result;
	result[ "kind" ] = "url_reorganization";
	result[ "total_count" ] = static_cast< Json::UInt64 >( total );
	result[ "unique_count" ] = static_cast< Json::UInt64 >( total - merged );
	result[ "merged_count" ] = static_cast< Json::UInt64 >( merged );
	result[ "sorted_count" ] = static_cast< Json::UInt64 >( sorted );
	co_return result;
}

static JobTask reorganizeUrlsJob( db::DBContext& db )
{
	auto progress { std::make_shared< jobs::UrlReorganizationProgress >() };
	co_await setJobProgress( progress );
	try
	{
		auto result {
			co_await folly::coro::co_withExecutor( getJobRuntime().executor(), reorganizeUrls( db, progress ) )
		};
		co_await setJobResponse( std::move( result ) );
	}
	catch ( ... )
	{
		progress->setPhase( "failed", "URL reorganization failed; earlier batches remain saved. Rerun to continue" );
		throw;
	}
}

static http::HTTPTask< http::Response > queueUrlReorganization(
	db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	const auto job { queueJob( reorganizeUrlsJob( db ), "Reorganizing URLs" ) };
	simdjson::builder::string_builder builder;
	builder.start_object();
	builder.append_key_value( "job_id", job->id() );
	builder.end_object();
	co_return jsonResponse( builder );
}

std::vector< http::RouteDefinition > jobRoutes( db::DBContext& db )
{
	std::vector< http::RouteDefinition > routes {};

	const auto add =
		[ &routes, &db ](
			const proxygen::HTTPMethod method,
			std::string path,
			std::vector< http::PathParameter > path_parameters,
			http::HTTPTask< http::Response > ( *handler )( db::DBContext&, http::RequestContext ) )
	{
		auto bound = [ &db, handler ]( http::RequestContext context ) -> http::HTTPTask< http::Response >
		{ co_return co_await handler( db, std::move( context ) ); };

		routes.push_back(
			http::RouteDefinition {
				http::RouteMetadata {
					method, std::move( path ), std::move( path_parameters ), { apiAuthValidation( db ) } },
				std::move( bound ) } );
	};

	add( proxygen::HTTPMethod::GET, "/jobs/status", {}, jobsStatus );
	add( proxygen::HTTPMethod::GET, "/jobs/{job_id}/status", { jobIdPathParameter() }, jobStatus );
	add( proxygen::HTTPMethod::POST, "/jobs/metadata/rescan", {}, rescanMetadata );
	add( proxygen::HTTPMethod::POST, "/jobs/urls/reorganize", {}, queueUrlReorganization );

	return routes;
}

} // namespace idhan::httpapi
