//
// Created by kj16609 on 3/11/25.
//

#include "IDHANTypes.hpp"
#include "api/IDHANRecordAPI.hpp"
#include "api/IDHANTagAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "api/helpers/tags/tags.hpp"
#include "fgl/defines.hpp"
#include "logging/log.hpp"
#include "splitTag.hpp"

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

	TagPair( const TagID id ) : tag_namespace(), tag_subtag(), tag_id( id ) {}

	static TagPair fromSplit( const std::string tag )
	{
		const auto& [ t_n, t_s ] = splitTag( tag );

		return TagPair( t_n, t_s );
	}

	TagPair( const Json::Value& value )
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

drogon::Task< std::expected< TagID, drogon::HttpResponsePtr > >
	getIDFromPair( const TagPair& tag, drogon::orm::DbClientPtr transaction )
{
	// convert any strings to their ids

	auto [ tag_id, tag_namespace, tag_subtag ] = tag;

	if ( tag_id.has_value() ) co_return tag_id.value();

	if ( std::holds_alternative< std::string >( tag_namespace ) )
	{
		const auto result { co_await findOrCreateNamespace( std::get< std::string >( tag_namespace ), transaction ) };

		if ( !result.has_value() ) co_return std::unexpected( result.error() );

		tag_namespace = result.value();
	}

	if ( std::holds_alternative< std::string >( tag_subtag ) )
	{
		const auto result { co_await findOrCreateSubtag( std::get< std::string >( tag_subtag ), transaction ) };

		if ( !result.has_value() ) co_return std::unexpected( result.error() );

		tag_subtag = result.value();
	}

	const auto result { co_await findOrCreateTag(
		std::get< NamespaceID >( tag_namespace ), std::get< SubtagID >( tag_subtag ), transaction ) };

	if ( !result.has_value() ) co_return std::unexpected( result.error() );

	co_return result.value();
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
			{
				// tag component
				tags.emplace_back( TagPair( item ) );
			}
			else if ( item.isUInt64() )
			{
				tags.emplace_back( static_cast< TagID >( item.asUInt64() ) );
			}
			else if ( item.isString() )
			{
				tags.emplace_back( TagPair::fromSplit( item.asString() ) );
			}
			else
				co_return std::unexpected( createBadRequest( "Invalid type format for json" ) );
		}

		co_return tags;
	}
	catch ( ... )
	{
		log::error( "Invalid json item" );
		co_return std::unexpected( createBadRequest( "Invalid json item in json list" ) );
	}
}

drogon::Task< std::expected< std::vector< TagID >, drogon::HttpResponsePtr > >
	getIDsFromPairs( const std::vector< TagPair >& pairs, drogon::orm::DbClientPtr transaction )
{
	std::vector< TagID > ids {};
	ids.reserve( pairs.size() );

	for ( const auto& pair : pairs )
	{
		const auto result { co_await getIDFromPair( pair, transaction ) };
		if ( !result.has_value() ) co_return std::unexpected( result.error() );
		ids.emplace_back( result.value() );
	}

	co_return ids;
}

drogon::Task< drogon::HttpResponsePtr > IDHANRecordAPI::
	addTags( const drogon::HttpRequestPtr request, RecordID record_id )
{
	// the path will contain a record_id
	// it will also contain a tag_domain_id as a extra parameter, if no parameter is specified, then it will instead use the 'default' domain

	const auto json_ptr { request->getJsonObject() };
	if ( json_ptr == nullptr ) co_return createBadRequest( "Json object malformed or null" );

	const auto db { drogon::app().getDbClient() };
	const auto transaction { co_await db->newTransactionCoro() };

	auto tag_pairs { co_await getTagPairs( *json_ptr ) };

	if ( !tag_pairs.has_value() ) co_return tag_pairs.error();

	auto tag_pair_ids { co_await getIDsFromPairs( std::move( tag_pairs.value() ), transaction ) };

	for ( const TagID& tag : tag_pair_ids.value() )
	{
		const auto tag_domain_id { helpers::getTagDomainID( request ) };

		if ( !tag_domain_id.has_value() ) co_return tag_domain_id.error();

		const auto tag_id { co_await getIDFromPair( tag, transaction ) };

		if ( !tag_id.has_value() ) co_return tag_id.error();

		const auto insert_result { co_await transaction->execSqlCoro(
			"INSERT INTO tag_mappings (record_id, tag_id, domain_id) VALUES ($1, $2, $3) ON CONFLICT DO NOTHING",
			record_id,
			tag_id.value(),
			tag_domain_id.value() ) };
	}

	co_return drogon::HttpResponse::newHttpResponse();
}

drogon::Task< drogon::HttpResponsePtr > IDHANRecordAPI::addMultipleTags( drogon::HttpRequestPtr request )
{
	const auto json_ptr { request->getJsonObject() };
	if ( json_ptr == nullptr ) co_return createBadRequest( "Json object malformed or null" );

	const auto db { drogon::app().getDbClient() };
	const auto transaction { co_await db->newTransactionCoro() };

	const auto& json { *json_ptr };

	auto tag_pairs { co_await getTagPairs( json[ "tags" ] ) };

	if ( !tag_pairs.has_value() ) co_return tag_pairs.error();

	auto tag_pair_ids { co_await getIDsFromPairs( std::move( tag_pairs.value() ), transaction ) };

	const auto records_json { json[ "records" ] };

	for ( const TagID tag_id : tag_pair_ids.value() )
	{
		const auto tag_domain_id { helpers::getTagDomainID( request ) };

		if ( !tag_domain_id.has_value() ) co_return tag_domain_id.error();

		for ( const auto& record_json : records_json )
		{
			if ( !record_json.isIntegral() )
				co_return createBadRequest( "Invalid json item in records list: Expected integral" );

			const auto insert_result { co_await transaction->execSqlCoro(
				"INSERT INTO tag_mappings (record_id, tag_id, domain_id) VALUES ($1, $2, $3) ON CONFLICT DO NOTHING",
				static_cast< RecordID >( record_json.asInt64() ),
				tag_id,
				tag_domain_id.value() ) };
		}
	}

	co_return drogon::HttpResponse::newHttpResponse();
}

} // namespace idhan::api