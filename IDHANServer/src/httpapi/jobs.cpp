#include "jobs.hpp"

#include <folly/coro/Collect.h>
#include <folly/io/IOBuf.h>
#include <json/json.h>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <simdjson.h>
#include <string>
#include <vector>

#include "IDHANTypes.hpp"
#include "apiAuth.hpp"
#include "fileMetadata.hpp"
#include "idValidation.hpp"
#include "idhan/db/DBContext.hpp"
#include "idhan/http/SSEBodySource.hpp"
#include "jobs/JobContext.hpp"
#include "jobs/JobTask.hpp"
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

static http::HTTPTask< http::Response > jobEvents(
	[[maybe_unused]] db::DBContext& db,
	[[maybe_unused]] http::RequestContext context )
{
	http::Response response {};
	response.body = proxygen::coro::HTTPSourceHolder { new http::SSEBodySource {
		std::move( response.message ), getJobRuntime().subscribe() } };
	co_return response;
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

	add( proxygen::HTTPMethod::GET, "/jobs/events", {}, jobEvents );
	add( proxygen::HTTPMethod::GET, "/jobs/status", {}, jobsStatus );
	add( proxygen::HTTPMethod::GET, "/jobs/{job_id}/status", { jobIdPathParameter() }, jobStatus );
	add( proxygen::HTTPMethod::POST, "/jobs/metadata/rescan", {}, rescanMetadata );

	return routes;
}

} // namespace idhan::httpapi
