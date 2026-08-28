#include <limits>

#include "IDHANTypes.hpp"
#include "api/RecordAPI.hpp"
#include "api/TagAPI.hpp"
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
	std::string tag_subtag;

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
			else if ( j_namespace.isString() )
			{
				tag_namespace = j_namespace.asString();
			}
			else
				throw std::invalid_argument( "Invalid tag namespace: Namespace was neither numeric nor string" );

			if ( j_subtag.isString() )
				tag_subtag = j_subtag.asString();
			else
				throw std::invalid_argument( "Invalid tag subtag: Subtag was not a string" );
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

	if ( std::holds_alternative< std::string >( tag_namespace ) )
	{
		const auto result { co_await db->execSqlCoro(
			"SELECT tag_id FROM tags JOIN tag_namespaces USING (namespace_id) WHERE "
			"namespace_text = CASEFOLD(NORMALIZE($1, NFC)) "
			"AND tags.subtag_text = NORMALIZE(CASEFOLD(NORMALIZE($2, NFC)), NFC)",
			std::get< std::string >( tag_namespace ),
			tag_subtag ) };

		if ( !result.empty() ) co_return result[ 0 ][ 0 ].as< TagID >();

		const auto create_status { co_await createTag( std::get< std::string >( tag_namespace ), tag_subtag, db ) };

		if ( create_status ) co_return *create_status;

		co_return std::unexpected( create_status.error()->genResponse() );
	}

	const auto create_status { co_await createTag( std::get< NamespaceID >( tag_namespace ), tag_subtag, db ) };

	if ( create_status ) co_return *create_status;

	co_return std::unexpected( create_status.error()->genResponse() );
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
			else if ( item.isIntegral() )
			{
				if ( !item.isInt64() || item.asInt64() <= 0 || item.asInt64() > std::numeric_limits< TagID >::max() )
					co_return std::unexpected( createBadRequest( "Invalid tag id {}: out of range", item.asString() ) );

				tags.emplace_back< TagPair >( static_cast< TagID >( item.asInt64() ) );
			}
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
	std::vector< TagID > ids( pairs.size(), INVALID_TAG_ID );

	std::vector< std::pair< std::string, std::string > > string_pairs {};
	std::vector< std::size_t > string_indices {};
	std::vector< std::size_t > other_indices {};

	for ( std::size_t i = 0; i < pairs.size(); ++i )
	{
		const auto& pair { pairs[ i ] };

		if ( pair.tag_id )
		{
			ids[ i ] = *pair.tag_id;
		}
		else if ( std::holds_alternative< std::string >( pair.tag_namespace ) )
		{
			string_indices.emplace_back( i );
			string_pairs.emplace_back( std::get< std::string >( pair.tag_namespace ), pair.tag_subtag );
		}
		else
		{
			other_indices.emplace_back( i );
		}
	}

	try
	{
		if ( !string_pairs.empty() )
		{
			const auto batch_ids { co_await createTagsFromPairs( string_pairs, db ) };
			if ( !batch_ids ) co_return std::unexpected( batch_ids.error() );

			for ( std::size_t k = 0; k < string_indices.size(); ++k )
				ids[ string_indices[ k ] ] = batch_ids.value()[ k ];
		}

		if ( !other_indices.empty() )
		{
			using Task = drogon::Task< std::expected< TagID, drogon::HttpResponsePtr > >;
			std::vector< Task > tasks {};
			tasks.reserve( other_indices.size() );
			for ( const auto index : other_indices ) tasks.emplace_back( getIDFromPair( pairs[ index ], db ) );

			const auto finished_tasks { co_await drogon::when_all( std::move( tasks ) ) };

			for ( std::size_t k = 0; k < other_indices.size(); ++k )
			{
				const auto& result { finished_tasks[ k ] };
				if ( !result ) co_return std::unexpected( result.error() );
				ids[ other_indices[ k ] ] = result.value();
			}
		}

		for ( const auto& id : ids )
			if ( id <= 0 )
				co_return std::unexpected( createBadRequest( "Tag ID was not valid. Must be tag_id > 0; Was {}", id ) );
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

drogon::Task< std::expected< void, drogon::HttpResponsePtr > > addTagsToRecords(
	std::vector< RecordID > record_ids,
	std::vector< TagID > tag_ids,
	const TagDomainID tag_domain_id,
	DbClientPtr db )
{
	if ( record_ids.empty() || tag_ids.empty() ) co_return std::expected< void, drogon::HttpResponsePtr > {};

	try
	{
		co_await db->execSqlCoro(
			"INSERT INTO tag_mappings (record_id, tag_id, tag_domain_id) "
			"SELECT r, t, $3 "
			"FROM UNNEST($1::INTEGER[]) AS r "
			"CROSS JOIN UNNEST($2::" TAG_PG_TYPE_NAME "[]) AS t "
			"ON CONFLICT DO NOTHING",
			std::move( record_ids ),
			std::move( tag_ids ),
			tag_domain_id );
	}
	catch ( std::exception& e )
	{
		co_return std::unexpected( createInternalError( "Error adding tags: {}", e.what() ) );
	}

	co_return std::expected< void, drogon::HttpResponsePtr > {};
}

drogon::Task< std::expected< void, drogon::HttpResponsePtr > > addTagSetsToRecords(
	const std::vector< RecordID >& record_ids,
	const std::vector< std::vector< TagID > >& tag_sets,
	const TagDomainID tag_domain_id,
	DbClientPtr db )
{
	FGL_ASSERT( record_ids.size() == tag_sets.size(), "record_ids and tag_sets must be the same size" );

	std::size_t total { 0 };
	for ( const auto& set : tag_sets ) total += set.size();

	std::vector< RecordID > flat_record_ids {};
	std::vector< TagID > flat_tag_ids {};
	flat_record_ids.reserve( total );
	flat_tag_ids.reserve( total );

	for ( std::size_t i = 0; i < record_ids.size(); ++i )
		for ( const auto tag_id : tag_sets[ i ] )
		{
			flat_record_ids.push_back( record_ids[ i ] );
			flat_tag_ids.push_back( tag_id );
		}

	if ( flat_record_ids.empty() ) co_return std::expected< void, drogon::HttpResponsePtr > {};

	try
	{
		co_await db->execSqlCoro(
			"INSERT INTO tag_mappings (record_id, tag_id, tag_domain_id) "
			"SELECT pairs.record_id, pairs.tag_id, $3 "
			"FROM UNNEST($1::INTEGER[], $2::" TAG_PG_TYPE_NAME "[]) AS pairs(record_id, tag_id) "
			"ON CONFLICT DO NOTHING",
			std::move( flat_record_ids ),
			std::move( flat_tag_ids ),
			tag_domain_id );
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
	// The path carries a record_id, and a tag_domain_id may be given as a query parameter. Without
	// one, the 'default' domain is used.

	const auto json_ptr { request->getJsonObject() };
	if ( json_ptr == nullptr ) co_return createBadRequest( "Json object malformed or null" );

	const auto db { drogon::app().getDbClient() };

	auto tag_pairs { co_await getTagPairs( *json_ptr ) };

	if ( !tag_pairs ) co_return tag_pairs.error();

	auto tag_pair_ids { co_await getIDsFromPairs( tag_pairs.value(), db ) };

	if ( !tag_pair_ids ) co_return tag_pair_ids.error();

	const auto tag_domain_id { helpers::getTagDomainIDParameter( request ) };

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

	// operator[] on a non-object root throws Json::LogicError, which would surface as a 500
	if ( !json.isObject() ) co_return createBadRequest( "Invalid json object. Expected object as root item" );

	if ( !json[ "records" ].isArray() )
		co_return createBadRequest( "Invalid json: Array of ids called 'records' must be present." );

	const auto tag_domain_id { helpers::getTagDomainIDParameter( request ) };

	if ( !tag_domain_id ) co_return tag_domain_id.error();

	if ( !( tag_domain_id.value() > 0 ) )
		co_return createBadRequest(
			"Invalid domain ID given: Expected tag_domain_id > 0; Got {}", tag_domain_id.value() );

	const auto domain { co_await findTagDomain( *tag_domain_id, db ) };

	if ( !domain )
		co_return createBadRequest( "Invalid domain ID given: Got no IDs (searched for {})", tag_domain_id.value() );

	const auto& records_json { json[ "records" ] };

	std::vector< RecordID > record_ids {};
	record_ids.reserve( records_json.size() );
	for ( const auto& record_json : records_json )
	{
		if ( !record_json.isIntegral() )
			co_return createBadRequest( "Invalid json item in records list: Expected integral" );
		record_ids.push_back( static_cast< RecordID >( record_json.asInt64() ) );
	}

	// unknown records would otherwise surface as FK-violation 500s in the mapping inserts
	const auto record_validation { co_await helpers::validateRecordIds( record_ids, db ) };
	if ( !record_validation ) co_return record_validation.error();

	// This list of tags is applied to all records. If it's null then there is no tags to apply from it.
	if ( const auto& tags_json = json[ "tags" ]; tags_json.isArray() )
	{
		const auto tag_pairs { co_await getTagPairs( tags_json ) };

		if ( !tag_pairs ) co_return tag_pairs.error();

		const auto tag_pair_ids { co_await getIDsFromPairs( tag_pairs.value(), db ) };

		if ( !tag_pair_ids ) co_return tag_pair_ids.error();

		const auto result { co_await addTagsToRecords( record_ids, tag_pair_ids.value(), tag_domain_id.value(), db ) };

		if ( !result ) co_return result.error();
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
			auto task = []( DbClientPtr db_c, const Json::Value set_json_current ) -> Task
			{
				const auto tags { co_await getTagPairs( set_json_current ) };

				if ( !tags ) co_return std::unexpected( tags.error() );

				const auto tag_ids_e { co_await getIDsFromPairs( tags.value(), db_c ) };

				if ( !tag_ids_e ) co_return std::unexpected( tag_ids_e.error() );

				co_return *tag_ids_e;
			};

			sets_processing_tasks.emplace_back( task( db, set_json ) );
		}

		auto sets { co_await drogon::when_all( std::move( sets_processing_tasks ) ) };

		std::vector< std::vector< TagID > > tag_sets {};
		tag_sets.reserve( sets_json.size() );

		for ( Json::ArrayIndex i = 0; i < sets_json.size(); ++i )
		{
			const auto& tag_ids_e { sets[ i ] };

			if ( !tag_ids_e ) co_return tag_ids_e.error();

			tag_sets.emplace_back( tag_ids_e.value() );
		}

		const auto result { co_await addTagSetsToRecords( record_ids, tag_sets, tag_domain_id.value(), db ) };

		if ( !result ) co_return result.error();
	}
	else if ( !sets_json.isNull() )
	{
		co_return createBadRequest( "Invalid json: Sets must be array or null (not present)" );
	}

	co_return drogon::HttpResponse::newHttpResponse();
}

} // namespace idhan::api
