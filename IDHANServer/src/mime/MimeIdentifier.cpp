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
	log::trace( "MimeIdentifier::test: entering test for '{}'", m_mime );

	for ( std::size_t i = 0; i < m_matchers.size(); ++i )
	{
		log::trace( "MimeIdentifier::test: '{}' running matcher {}/{}", m_mime, i + 1, m_matchers.size() );
		if ( !co_await m_matchers[ i ]->test( cursor ) )
		{
			log::trace( "MimeIdentifier::test: '{}' matcher {} failed", m_mime, i + 1 );
			co_return false;
		}
		log::trace( "MimeIdentifier::test: '{}' matcher {} passed", m_mime, i + 1 );
	}

	if ( this->m_require_extension )
	{
		auto extension { cursor.fileExtension() };
		if ( extension.starts_with( '.' ) ) extension = extension.substr( 1 );

		log::trace(
			"MimeIdentifier::test: '{}' checking extension '{}' against required extensions", m_mime, extension );
		if ( extension.empty() )
		{
			log::trace( "MimeIdentifier::test: '{}' extension was empty, failing", m_mime );
			co_return false;
		}

		for ( const auto& match_extension : m_extensions )
		{
			if ( extension == match_extension )
			{
				log::trace( "MimeIdentifier::test: '{}' extension '{}' matched", m_mime, extension );
				co_return true;
			}
		}

		log::trace(
			"MimeIdentifier::test: '{}' extension '{}' did not match any required extension", m_mime, extension );
		co_return false;
	}

	log::trace( "MimeIdentifier::test: '{}' all matchers passed", m_mime );
	co_return true;
}

MimeIdentifier::MimeIdentifier( const Json::Value& json )
{
	if ( !json.isMember( "mime" ) || !json[ "mime" ].isString() )
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
		if ( !extension.isString() ) throw std::runtime_error( "extensions array entries must be strings" );
		m_extensions.emplace_back( extension.asString() );
	}

	if ( !m_extensions.empty() ) m_best_extension = m_extensions.at( 0 );

	if ( json.isMember( "priority" ) )
	{
		// MimeScore is unsigned, so a negative value would wrap into a top-priority score
		if ( !json[ "priority" ].isIntegral() || json[ "priority" ].asInt() < 0 )
			throw std::runtime_error( "priority must be a non-negative integer" );
		m_priority = static_cast< MimeScore >( json[ "priority" ].asInt() );
	}
	else
		m_priority = 25;

	if ( json.isMember( "data" ) )
		m_matchers = parseDataJson( json[ "data" ] );
	else
		log::warn( "Mime parser for {} did not have any matchers", m_mime );

	if ( json.isMember( "require_extension" ) ) m_require_extension = json[ "require_extension" ].asBool();
}

MimeIdentifier::MimeIdentifier( const std::filesystem::path& path ) : MimeIdentifier( jsonFromFile( path ) )
{}
} // namespace idhan::mime
