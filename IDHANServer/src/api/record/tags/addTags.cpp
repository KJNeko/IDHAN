//
// Created by kj16609 on 3/11/25.
//

#include "IDHANTypes.hpp"
#include "api/IDHANRecordAPI.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "api/helpers/helpers.hpp"
#include "fgl/defines.hpp"
#include "logging/log.hpp"
#include "splitTag.hpp"

namespace idhan::api
{

drogon::Task< drogon::HttpResponsePtr > IDHANRecordAPI::
	addTags( const drogon::HttpRequestPtr request, RecordID record_id )
{
	// the path will contain a record_id
	// it will also contain a tag_domain_id as a extra parameter, if no parameter is specified, then it will instead use the 'default' domain

	struct TagPair
	{
		std::variant< NamespaceID, std::string > tag_namespace;
		std::variant< SubtagID, std::string > tag_subtag;

		TagPair( std::string n_tag, std::string n_subtag ) : tag_namespace( n_tag ), tag_subtag( n_subtag ) {}

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

	std::vector< std::variant< TagID, TagPair, std::string > > tags {};

	const auto json_ptr { request->getJsonObject() };
	if ( json_ptr == nullptr ) co_return createBadRequest( "Json object malformed or null" );

	const Json::Value& json { *json_ptr };

	const auto db { drogon::app().getDbClient() };

	tags.reserve( json.size() );

	FGL_ASSERT( json.isArray(), "Json was not array!" );

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
				tags.emplace_back( item.asString() );
			}
			else
				co_return createBadRequest( "Invalid type format for json" );
		}
	}
	catch ( ... )
	{
		log::error( "Invalid json item" );
		std::rethrow_exception( std::current_exception() );
	}

	const auto transaction { co_await db->newTransactionCoro() };

	for ( auto& tag : tags )
	{
		// break apart std::string
		if ( std::holds_alternative< std::string >( tag ) )
		{
			const auto [ n_tag, s_tag ] { splitTag( std::get< std::string >( tag ) ) };
			log::trace( "Split tag {} into {}:{}", std::get< std::string >( tag ), n_tag, s_tag );
			tag = TagPair( n_tag, s_tag );
		}

		// convert any strings to their ids

		if ( std::holds_alternative< TagPair >( tag ) )
		{
			auto& [ tag_namespace, tag_subtag ] = std::get< TagPair >( tag );

			if ( std::holds_alternative< std::string >( tag_namespace ) )
			{
				const auto result { co_await db->execSqlCoro(
					"SELECT namespace_id FROM tag_namespaces WHERE namespace_text = $1",
					std::get< std::string >( tag_namespace ) ) };

				if ( result.empty() )
					co_return createBadRequest(
						"Could not find namespace id for given namespace: {}",
						std::get< std::string >( tag_namespace ) );

				log::trace(
					"Got namespace {} from {}",
					result[ 0 ][ 0 ].as< NamespaceID >(),
					std::get< std::string >( tag_namespace ) );
				tag_namespace = result[ 0 ][ 0 ].as< NamespaceID >();
			}

			if ( std::holds_alternative< std::string >( tag_subtag ) )
			{
				const auto result { co_await db->execSqlCoro(
					"SELECT subtag_id FROM tag_subtags WHERE subtag_text = $1",
					std::get< std::string >( tag_subtag ) ) };

				if ( result.empty() )
					co_return createBadRequest(
						"Could not find subtag id for given subtag: {}", std::get< std::string >( tag_subtag ) );

				log::trace(
					"Got subtag {} from {}", result[ 0 ][ 0 ].as< SubtagID >(), std::get< std::string >( tag_subtag ) );
				tag_subtag = result[ 0 ][ 0 ].as< SubtagID >();
			}

			const auto result { co_await db->execSqlCoro(
				"SELECT tag_id FROM tags WHERE namespace_id = $1 AND subtag_id = $2",
				std::get< NamespaceID >( tag_namespace ),
				std::get< SubtagID >( tag_subtag ) ) };

			if ( result.empty() )
				co_return createBadRequest(
					"Could not find given tag for {}:{}",
					std::get< NamespaceID >( tag_namespace ),
					std::get< SubtagID >( tag_subtag ) );

			log::trace(
				"Got tag {} from {}:{}",
				result[ 0 ][ 0 ].as< TagID >(),
				std::get< NamespaceID >( tag_namespace ),
				std::get< SubtagID >( tag_subtag ) );
			tag = result[ 0 ][ 0 ].as< TagID >();
		}

		if ( !std::holds_alternative< TagID >( tag ) )
			co_return createInternalError( "Expected variant to be in TagID, Was not" );

		const auto tag_domain_id { helpers::getTagDomainID( request ) };

		if ( !tag_domain_id.has_value() ) co_return tag_domain_id.error();

		const TagID tag_id { std::get< TagID >( tag ) };

		const auto insert_result { co_await transaction->execSqlCoro(
			"INSERT INTO tag_mappings (record_id, tag_id, domain_id) VALUES ($1, $2, $3)",
			record_id,
			tag_id,
			tag_domain_id.value() ) };
	}

	co_return drogon::HttpResponse::newHttpResponse();
}

} // namespace idhan::api