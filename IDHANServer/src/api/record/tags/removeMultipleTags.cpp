#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "logging/ScopedTimer.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > RecordAPI::removeMultipleTags( drogon::HttpRequestPtr request )
{
	logging::ScopedTimer timer { "removeMultipleTags" };
	const auto json_ptr { request->getJsonObject() };
	if ( json_ptr == nullptr ) co_return createBadRequest( "Json object malformed or null" );

	const auto& json { *json_ptr };

	// operator[] on a non-object root throws Json::LogicError, which would surface as a 500
	if ( !json.isObject() ) co_return createBadRequest( "Invalid json object. Expected object as root item" );

	if ( !json[ "records" ].isArray() )
		co_return createBadRequest( "Invalid json: Array of ids called 'records' must be present." );

	if ( !json[ "sets" ].isArray() )
		co_return createBadRequest( "Invalid json: Array of tag sets called 'sets' must be present." );

	const auto tag_domain_id { helpers::getTagDomainIDParameter( request ) };
	if ( !tag_domain_id ) co_return tag_domain_id.error();

	const auto& records_json { json[ "records" ] };
	const auto& sets_json { json[ "sets" ] };

	if ( sets_json.size() != records_json.size() )
		co_return createBadRequest(
			"Sets vs Records size mismatch. Number of sets must match number of records. Got {} expected {}",
			sets_json.size(),
			records_json.size() );

	std::vector< RecordID > record_ids;
	record_ids.reserve( records_json.size() );

	for ( const auto& record_json : records_json )
	{
		if ( !record_json.isIntegral() )
			co_return createBadRequest( "Invalid json item in records list: Expected integral" );
		record_ids.push_back( static_cast< RecordID >( record_json.asInt64() ) );
	}

	std::vector< std::vector< TagID > > tag_sets;
	tag_sets.reserve( sets_json.size() );

	for ( const auto& set_json : sets_json )
	{
		if ( !set_json.isArray() ) co_return createBadRequest( "Invalid json: Each set must be an array of tag_ids" );

		std::vector< TagID > tag_ids;
		tag_ids.reserve( set_json.size() );

		for ( const auto& item : set_json )
		{
			if ( !item.isIntegral() ) co_return createBadRequest( "Invalid tag_id in set: Must be integral" );
			tag_ids.push_back( item.as< TagID >() );
		}

		tag_sets.push_back( std::move( tag_ids ) );
	}

	auto db { drogon::app().getDbClient() };

	try
	{
		using Task = drogon::Task< drogon::HttpResponsePtr >;
		std::vector< Task > tasks;
		tasks.reserve( record_ids.size() );

		for ( std::size_t i = 0; i < record_ids.size(); ++i )
		{
			if ( tag_sets[ i ].empty() ) continue;

			auto task =
				[]( DbClientPtr db_c,
			        const RecordID record_id,
			        std::vector< TagID > tag_ids,
			        const TagDomainID domain_id ) -> Task
			{
				co_await db_c->execSqlCoro(
					"DELETE FROM tag_mappings WHERE record_id = $1 AND tag_id IN (SELECT UNNEST($2::" TAG_PG_TYPE_NAME
					"[])) AND tag_domain_id = $3",
					record_id,
					std::move( tag_ids ),
					domain_id );
				co_return nullptr;
			};

			tasks.emplace_back( task( db, record_ids[ i ], std::move( tag_sets[ i ] ), tag_domain_id.value() ) );
		}

		co_await drogon::when_all( std::move( tasks ) );
	}
	catch ( std::exception& e )
	{
		co_return createInternalError( "Error removing tags: {}", e.what() );
	}

	Json::Value ok {};
	ok[ "status" ] = 200;

	co_return drogon::HttpResponse::newHttpJsonResponse( ok );
}

} // namespace idhan::api
