//
// Created by kj16609 on 3/11/25.
//

#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "db/drogonArrayBind.hpp"
#include "fgl/defines.hpp"
#include "logging/ScopedTimer.hpp"
#include "logging/log.hpp"
#include "splitTag.hpp"
#include "tags/tags.hpp"

namespace idhan::api
{

struct TagPair
{
	std::optional< TagID > tag_id;
	std::variant< NamespaceID, std::string > tag_namespace;
	std::variant< SubtagID, std::string > tag_subtag;

	TagPair( std::string n_tag, std::string n_subtag ) :
	  tag_id( std::nullopt ),
	  tag_namespace( n_tag ),
	  tag_subtag( n_subtag )
	{}

	TagPair( const TagID id ) : tag_id( id ), tag_namespace(), tag_subtag() {}

	static TagPair fromSplit( const std::string tag )
	{
		const auto& [ t_n, t_s ] = splitTag( tag );

		return TagPair( t_n, t_s );
	}

	explicit TagPair( const Json::Value& value ) : tag_id( std::nullopt ), tag_namespace(), tag_subtag()
	{
		try
		{
			FGL_ASSERT( value.isObject(), "Invalid JSON value for TagPair" );
			const auto& j_namespace { value[ "namespace" ] };
			const auto& j_subtag { value[ "subtag" ] };

			if ( j_namespace.isIntegral() )
			{
				tag_namespace = j_namespace.as< NamespaceID >();
			}
			else if ( j_subtag.isString() )
			{
				tag_namespace = j_namespace.asString();
			}
			else
				throw std::invalid_argument( "Invalid tag namespace" );

			if ( j_subtag.isIntegral() )
				tag_subtag = j_subtag.as< SubtagID >();
			else if ( j_subtag.isString() )
				tag_subtag = j_subtag.asString();
			else
				throw std::invalid_argument( "Invalid tag subtag: Subtag was neither numeric nor string" );
		}
		catch ( ... )
		{
			log::error( "Invalid tag pair" );
			std::rethrow_exception( std::current_exception() );
		}
	}
};

drogon::Task< std::expected< TagID, drogon::HttpResponsePtr > > getIDFromPair( const TagPair& tag, DbClientPtr db )
{
	// convert any strings to their ids

	auto [ tag_id, tag_namespace, tag_subtag ] = tag;

	if ( tag_id ) co_return tag_id.value();

	const auto tag_namespace_is_str { std::holds_alternative< std::string >( tag_namespace ) };
	const auto tag_subtag_is_str { std::holds_alternative< std::string >( tag_subtag ) };

	if ( tag_namespace_is_str && tag_subtag_is_str )
	{
		const auto result { co_await db->execSqlCoro(
			"SELECT tag_id FROM tags JOIN tag_namespaces USING (namespace_id) JOIN tag_subtags USING (subtag_id) WHERE "
			"namespace_text = $1 AND subtag_text = $2",
			std::get< std::string >( tag_namespace ),
			std::get< std::string >( tag_subtag ) ) };

		if ( !result.empty() ) co_return result[ 0 ][ 0 ].as< TagID >();

		const auto create_status {
			co_await createTag( std::get< std::string >( tag_namespace ), std::get< std::string >( tag_subtag ), db )
		};

		if ( create_status ) co_return *create_status;

		co_return std::unexpected( create_status.error()->genResponse() );
	}

	if ( !tag_namespace_is_str && !tag_subtag_is_str )
	{
		const auto result { co_await db->execSqlCoro(
			"INSERT INTO tags (namespace_id, subtag_id) VALUES ($1, $2) RETURNING tag_id",
			std::get< NamespaceID >( tag_namespace ),
			std::get< SubtagID >( tag_subtag ) ) };

		if ( !result.empty() ) co_return result[ 0 ][ 0 ].as< TagID >();
		co_return std::unexpected( createInternalError(
			R"(Failed to insert tag '{}':'{}')",
			std::get< NamespaceID >( tag_namespace ),
			std::get< SubtagID >( tag_subtag ) ) );
	}

	co_return std::unexpected(
		createInternalError( "Failed to get ID from pair, Namespace and Subtag must both be String or Integers" ) );
}

drogon::Task< std::expected< std::vector< TagPair >, drogon::HttpResponsePtr > > getTagPairs( const Json::Value& json )
{
	std::vector< TagPair > tags {};
	tags.reserve( json.size() );

	if ( !json.isArray() ) co_return std::unexpected( createBadRequest( "Invalid json tag array: Was not array" ) );

	try
	{
		for ( const auto& item : json )
		{
			if ( item.isObject() )
				tags.emplace_back( TagPair( item ) );
			else if ( item.isUInt64() )
				tags.emplace_back< TagPair >( static_cast< TagID >( item.asUInt64() ) );
			else if ( item.isString() )
				tags.emplace_back( TagPair::fromSplit( item.asString() ) );
			else
				co_return std::unexpected( createBadRequest( "Invalid type format for json" ) );
		}

		co_return tags;
	}
	catch ( std::exception& e )
	{
		log::error( "Error with {}", json.toStyledString() );
		co_return std::unexpected( createBadRequest( "Invalid json item in json list: {}", e.what() ) );
	}
	catch ( ... )
	{
		log::error( "Invalid json item" );
		co_return std::unexpected( createBadRequest( "Invalid json item in json list" ) );
	}
}

drogon::Task< std::expected< std::vector< TagID >, drogon::HttpResponsePtr > > getIDsFromPairs(
	const std::vector< TagPair >& pairs,
	DbClientPtr db )
{
	std::vector< TagID > ids {};
	ids.reserve( pairs.size() );

	try
	{
		using Task = drogon::Task< std::expected< TagID, drogon::HttpResponsePtr > >;
		std::vector< Task > tasks {};

		for ( const auto& pair : pairs )
		{
			tasks.emplace_back( getIDFromPair( pair, db ) );
		}

		const auto finished_tasks { co_await drogon::when_all( std::move( tasks ) ) };

		for ( const std::expected< TagID, drogon::HttpResponsePtr >& result : finished_tasks )
		{
			// const auto result { co_await getIDFromPair( pair, db ) };
			if ( !result ) co_return std::unexpected( result.error() );
			ids.emplace_back( result.value() );

			if ( result.value() <= 0 )
			{
				co_return std::unexpected(
					createBadRequest( "Tag ID was not valid. Must be tag_id > 0; Was {}", result.value() ) );
			}
		}
	}
	catch ( std::exception& e )
	{
		co_return std::unexpected( createBadRequest( "Error getting tag ids from pairs: {}", e.what() ) );
	}
	catch ( ... )
	{
		co_return std::unexpected( createBadRequest( "Error getting tag ids from pairs" ) );
	}

	co_return ids;
}

drogon::Task< std::expected< void, drogon::HttpResponsePtr > > addTagsToRecord(
	const RecordID record_id,
	std::vector< TagID > tag_ids,
	const TagDomainID tag_domain_id,
	DbClientPtr db )
{
	try
	{
		const auto insert_result { co_await db->execSqlCoro(
			"INSERT INTO tag_mappings (record_id, tag_id, tag_domain_id) VALUES ($1, UNNEST($2::" TAG_PG_TYPE_NAME
			"[]), $3) ON CONFLICT DO NOTHING",
			record_id,
			std::move( tag_ids ),
			tag_domain_id ) };
	}
	catch ( std::exception& e )
	{
		co_return std::unexpected( createInternalError( "Error adding tags: {}", e.what() ) );
	}

	co_return std::expected< void, drogon::HttpResponsePtr > {};
}

drogon::Task< drogon::HttpResponsePtr > RecordAPI::addTags(
	const drogon::HttpRequestPtr request,
	const RecordID record_id )
{
	logging::ScopedTimer timer { "addTags" };
	// the path will contain a record_id
	// it will also contain a tag_domain_id as a extra parameter, if no parameter is specified, then it will instead use
	// the 'default' domain

	const auto json_ptr { request->getJsonObject() };
	if ( json_ptr == nullptr ) co_return createBadRequest( "Json object malformed or null" );

	const auto db { drogon::app().getDbClient() };

	auto tag_pairs { co_await getTagPairs( *json_ptr ) };

	if ( !tag_pairs ) co_return tag_pairs.error();

	auto tag_pair_ids { co_await getIDsFromPairs( tag_pairs.value(), db ) };

	if ( !tag_pair_ids ) co_return tag_pair_ids.error();

	const auto tag_domain_id { helpers::getTagDomainID( request ) };

	if ( !tag_domain_id ) co_return tag_domain_id.error();

	for ( const auto& tag_id : tag_pair_ids.value() ) FGL_ASSERT( tag_id > 0, "TagID must be above 0" );

	const auto result {
		co_await addTagsToRecord( record_id, std::move( tag_pair_ids.value() ), tag_domain_id.value(), db )
	};

	if ( !result ) co_return result.error();

	co_return drogon::HttpResponse::newHttpResponse();
}

drogon::Task< drogon::HttpResponsePtr > RecordAPI::addMultipleTags( drogon::HttpRequestPtr request )
{
	logging::ScopedTimer timer { "addMultipleTags" };
	const auto json_ptr { request->getJsonObject() };
	if ( json_ptr == nullptr ) co_return createBadRequest( "Json object malformed or null" );

	const auto db { drogon::app().getDbClient() };

	const auto& json { *json_ptr };

	if ( !json[ "records" ].isArray() )
		co_return createBadRequest( "Invalid json: Array of ids called 'records' must be present." );

	const auto tag_domain_id { helpers::getTagDomainID( request ) };

	if ( !tag_domain_id ) co_return tag_domain_id.error();

	if ( !( tag_domain_id.value() > 0 ) )
		co_return createBadRequest(
			"Invalid domain ID given: Expected tag_domain_id > 0; Got {}", tag_domain_id.value() );

	const auto domain_search { co_await db->execSqlCoro(
		"SELECT tag_domain_id FROM tag_domains WHERE tag_domain_id = $1", tag_domain_id.value() ) };

	if ( domain_search.empty() )
		co_return createBadRequest( "Invalid domain ID given: Got no IDs (searched for {})", tag_domain_id.value() );

	const auto& records_json { json[ "records" ] };

	std::vector< drogon::Task< std::expected< void, drogon::HttpResponsePtr > > > add_results {};

	// This list of tags is applied to all records. If it's null then there is no tags to apply from it.
	if ( const auto& tags_json = json[ "tags" ]; tags_json.isArray() )
	{
		const auto tag_pairs { co_await getTagPairs( tags_json ) };

		if ( !tag_pairs ) co_return tag_pairs.error();

		auto tag_pair_ids { co_await getIDsFromPairs( tag_pairs.value(), db ) };

		if ( !tag_pair_ids ) co_return tag_pair_ids.error();

		for ( const auto& record_json : records_json )
		{
			if ( !record_json.isIntegral() )
				co_return createBadRequest( "Invalid json item in records list: Expected integral" );

			// const auto result { co_await addTagsToRecord(
			// 	static_cast< RecordID >( record_json.asInt64() ),
			// 	std::move( tag_pair_ids.value() ),
			// 	tag_domain_id.value(),
			// 	db ) };

			// if ( !result ) co_return result.error();

			add_results.emplace_back( addTagsToRecord(
				static_cast< RecordID >( record_json.asInt64() ),
				std::move( tag_pair_ids.value() ),
				tag_domain_id.value(),
				db ) );
		}
	}
	else if ( !tags_json.isNull() )
	{
		co_return createBadRequest( "Invalid json: Tags must be array or null (not present)" );
	}

	if ( const auto& sets_json = json[ "sets" ]; sets_json.isArray() )
	{
		if ( sets_json.size() != records_json.size() )
			co_return createBadRequest(
				"Sets vs Records size mismatch. Number of sets must match number of records. Got {} expected {}: Json: {}",
				sets_json.size(),
				records_json.size(),
				json.toStyledString() );

		using Task = drogon::Task< std::expected< std::vector< TagID >, drogon::HttpResponsePtr > >;
		std::vector< Task > sets_processing_tasks {};
		sets_processing_tasks.reserve( records_json.size() );

		for ( const auto& set_json : sets_json )
		{
			auto task = [ db ]( const Json::Value set_json_current ) -> Task
			{
				const auto tags { co_await getTagPairs( set_json_current ) };

				if ( !tags ) co_return std::unexpected( tags.error() );

				const auto tag_ids_e { co_await getIDsFromPairs( tags.value(), db ) };

				if ( !tag_ids_e ) co_return std::unexpected( tag_ids_e.error() );

				co_return *tag_ids_e;
			};

			sets_processing_tasks.emplace_back( task( set_json ) );
		}

		auto sets { co_await drogon::when_all( std::move( sets_processing_tasks ) ) };

		for ( Json::ArrayIndex i = 0; i < sets_json.size(); ++i )
		{
			// each set will be an array of tags
			/*
			const auto tags { co_await getTagPairs( sets_json[ i ] ) };

			if ( !tags ) co_return tags.error();

			const auto tag_ids_e { co_await getIDsFromPairs( tags.value(), db ) };

			if ( !tag_ids_e ) co_return tag_ids_e.error();
			*/
			const auto& tag_ids_e { sets[ i ] };

			auto tag_ids { tag_ids_e.value() };

			// const auto result { co_await addTagsToRecord(
			// 	static_cast< RecordID >( records_json[ i ].asInt64() ),
			// 	std::move( tag_ids ),
			// 	tag_domain_id.value(),
			// 	db ) };

			const auto record_json { records_json[ i ] };

			add_results.emplace_back( addTagsToRecord(
				static_cast< RecordID >( record_json.asInt64() ), std::move( tag_ids ), tag_domain_id.value(), db ) );

			// if ( !result ) co_return result.error();
		}
	}
	else if ( !sets_json.isNull() )
	{
		co_return createBadRequest( "Invalid json: Sets must be array or null (not present)" );
	}

	const auto await_result { co_await drogon::when_all( std::move( add_results ) ) };

	for ( const auto& result : await_result )
	{
		if ( !result )
		{
			co_return result.error();
		}
	}

	co_return drogon::HttpResponse::newHttpResponse();
}

} // namespace idhan::api
