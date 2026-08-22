#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "metadata/metadata.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::fetchInfo(
	[[maybe_unused]] drogon::HttpRequestPtr request,
	RecordID record_id )
{
	const auto db { drogon::app().getDbClient() };

	auto batch { co_await metadata::collectRecordInfo( { record_id }, db ) };

	if ( batch.records.empty() ) co_return createNotFound( "Record ID was not found" );

	// The single-record view is where an old record heals itself: parse it now and collect again so
	// the caller gets the file specific fields rather than parsed:false.
	if ( batch.records[ 0 ][ "parsed" ].isBool() && !batch.records[ 0 ][ "parsed" ].asBool() )
	{
		const auto parse_result { co_await metadata::tryParseRecordMetadata( record_id, db ) };
		if ( !parse_result ) co_return parse_result.error();

		batch = co_await metadata::collectRecordInfo( { record_id }, db );

		if ( batch.records.empty() ) co_return createNotFound( "Record ID was not found" );
	}

	co_return drogon::HttpResponse::newHttpJsonResponse( std::move( batch.records[ 0 ] ) );
}

drogon::Task< drogon::HttpResponsePtr > RecordAPI::parseFile( drogon::HttpRequestPtr request, RecordID record_id )
{
	const auto db { drogon::app().getDbClient() };
	const auto parse_result { co_await metadata::tryParseRecordMetadata( record_id, db ) };
	if ( !parse_result ) co_return parse_result.error();

	co_return co_await fetchInfo( request, record_id );
}

} // namespace idhan::api
