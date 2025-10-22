//
// Created by kj16609 on 10/21/25.
//
#include "MimeIdentifier.hpp"

#include <json/reader.h>
#include <json/value.h>

#include <fstream>

#include "Cursor.hpp"
#include "logging/log.hpp"

namespace idhan::mime
{

Json::Value jsonFromFile( const std::filesystem::path& path )
{
	if ( std::ifstream ifs( path ); ifs )
	{
		Json::Value json {};
		Json::Reader reader {};
		if ( !reader.parse( ifs, json ) )
		{
			throw std::runtime_error( "Failed to parse JSON file" );
		}

		return json;
	}
	throw std::runtime_error( "Could not open file" );
}

drogon::Task< bool > MimeIdentifier::test( const Cursor cursor ) const
{
	for ( const auto& matcher : m_matchers )
	{
		if ( !co_await matcher->test( cursor ) ) co_return false;
	}

	co_return true;
}

MimeIdentifier::MimeIdentifier( const Json::Value& json )
{
	if ( !json.isMember( "mime" ) )
	{
		throw std::runtime_error( "Missing mime field" );
	}

	m_mime = json[ "mime" ].asString();

	if ( !json.isMember( "extensions" ) )
	{
		throw std::runtime_error( "Missing extensions array" );
	}

	for ( const auto& extension : json[ "extensions" ] )
	{
		if ( !extension.isString() ) throw std::runtime_error( "Missing extensions array" );
		m_extensions.emplace_back( extension.asString() );
	}

	if ( !m_extensions.empty() ) m_best_extension = m_extensions.at( 0 );

	if ( json.isMember( "priority" ) )
		m_priority = static_cast< std::size_t >( json[ "priority" ].asInt() );
	else
		m_priority = 25;

	if ( json.isMember( "data" ) )
		m_matchers = parseDataJson( json[ "data" ] );
	else
		log::warn( "Mime parser for {} did not have any matchers", m_mime );
}

MimeIdentifier::MimeIdentifier( const std::filesystem::path& path ) : MimeIdentifier( jsonFromFile( path ) )
{}
} // namespace idhan::mime