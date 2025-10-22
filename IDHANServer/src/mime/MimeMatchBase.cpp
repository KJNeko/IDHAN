//
// Created by kj16609 on 10/21/25.
//

#include "MimeMatchBase.hpp"

#include "Cursor.hpp"
#include "logging/log.hpp"
#include "matchers/MimeMatchInclude.hpp"
#include "matchers/MimeMatchSearch.hpp"

namespace idhan::mime
{

MimeMatcher createMatchFromJson( const Json::Value& json )
{
	if ( !json.isMember( "type" ) || !json[ "type" ].isString() )
		throw std::runtime_error( "Expected a field `type` that is a string" );

	using MimeMatcherFactory = std::function< MimeMatcher( const Json::Value& ) >;

	static std::unordered_map< std::string, MimeMatcherFactory > matcher_factories {
		{ "search", MimeMatchSearch::createFromJson }, { "include", MimeMatchInclude::createFromJson }
	};

	const std::string type { json[ "type" ].asString() };

	if ( auto itter = matcher_factories.find( type ); itter != matcher_factories.end() )
	{
		return itter->second( json );
	}

	throw std::runtime_error( "Unknown matcher type `" + type + "`" );
}

std::vector< MimeMatcher > parseDataJson( const Json::Value& json )
{
	if ( !json.isArray() )
	{
		throw std::runtime_error( "Expected data to be an array of objects" );
	}

	std::vector< MimeMatcher > matchers {};

	for ( const auto& matcher_json : json )
	{
		if ( !matcher_json.isObject() ) throw std::runtime_error( "Expected data to be an object" );

		matchers.emplace_back( createMatchFromJson( matcher_json ) );
	}

	return matchers;
}

drogon::Task< bool > MimeMatchBase::test( Cursor cursor )
{
	const auto does_match { co_await this->match( cursor ) };

	if ( !does_match ) co_return false;

	log::debug( "Test passed. Checking {} children", m_children.size() );

	for ( const auto& child : m_children )
	{
		if ( !co_await child->test( cursor ) )
		{
			log::debug( "Child failed" );
			co_return false;
		}
	}

	co_return true;
}

MimeMatchBase::MimeMatchBase( const Json::Value& json )
{
	if ( !json.isObject() )
	{
		throw std::runtime_error( "Expected a JSON object to be an object" );
	}

	if ( json.isMember( "data" ) )
	{
		if ( !json[ "data" ].isArray() )
		{
			throw std::runtime_error( "Expected data to be an array of objects" );
		}

		// Has children
		m_children = parseDataJson( json[ "data" ] );
	}
}

} // namespace idhan::mime
