//
// Created by kj16609 on 12/18/24.
//
#include "MimeDatabase.hpp"

#include <json/json.h>

#include <cassert>
#include <filesystem>
#include <fstream>

#include "decodeHex.hpp"
#include "logging/log.hpp"

namespace idhan::mime
{
inline static std::shared_ptr< MimeDatabase > mime_db { nullptr };

bool MimeDataIdentifier::
	test( const std::byte* data, const std::size_t length, std::size_t& current_offset, MimeDataTrack& context ) const
{
	// test this level of condition
	if ( pass( data, length, current_offset, context ) )
	{
		// test all children
		for ( const auto& child : m_children )
		{
			if ( !child->test( data, length, current_offset, context ) )
			{
				// if child fails to pass, return false
				return false;
			}
		}

		// all children passed, return true.
		return true;
	}

	return false;
}

using IdentifierType = MimeIdentifier::Type;

IdentifierType getIdentifierType( const std::string& str )
{
	if ( str.empty() ) throw std::runtime_error( "Failed to get identifier type: Was empty" );

	if ( str == "search" )
	{
		return IdentifierType::Search;
	}

	if ( str == "override" )
	{
		return IdentifierType::Override;
	}

	throw std::runtime_error( "Failed to get identifier type: " + str );
}

std::unique_ptr< MimeDataIdentifier > parseIdentifier( const Json::Value& root )
{
	// get the type
	if ( !root.isObject() )
		throw std::runtime_error( std::format( "Failed to parse identifiers from json object: Expected object." ) );

	//Check that `type` exists
	const auto& type_str { root[ "type" ] };
	if ( !type_str.isString() ) throw std::runtime_error( std::format( "Invalid type for member 'type'" ) );

	const auto type { getIdentifierType( type_str.asString() ) };

	switch ( type )
	{
		default:
			throw std::runtime_error( "Unknown identifier type for json object" );
		case IdentifierType::Search:
			{
				return std::make_unique< DataIdentifierSearch >( root );
			}
		case IdentifierType::Override:
			{
				return std::make_unique< DataIdentifierOverride >( root );
			}
	}

	throw std::runtime_error( "Failed to parse identifier type: " + type_str.asString() );
}

MimeDataIdentifier::MimeDataIdentifier( const Json::Value& value )
{
	if ( value.isNull() && !value.isArray() ) return;

	for ( const auto& child : value )
	{
		m_children.emplace_back( parseIdentifier( child ) );
	}
}

bool DataIdentifierSearch::
	passOffset( const std::byte* data, const std::size_t length, std::size_t& current_offset, MimeDataTrack& context )
		const
{
	std::int64_t set_offset { static_cast< std::int64_t >( current_offset ) + offset };

	//wrap around
	if ( set_offset < 0 ) set_offset = length + set_offset;

	if ( set_offset + m_data.size() >= length ) return false;

	for ( std::size_t i = 0; i < m_data.size(); ++i )
	{
		if ( m_data[ i ] != data[ i + set_offset ] ) return false;
	}

	current_offset += m_data.size();
	return true;
}

bool DataIdentifierSearch::
	passNoOffset( const std::byte* data, const std::size_t length, std::size_t& current_offset, MimeDataTrack& context )
		const
{
	if ( length <= m_data.size() ) return false;

	for ( std::size_t x = 0; x < length - m_data.size(); ++x )
	{
		for ( std::size_t i = 0; i < m_data.size(); ++i )
		{
			if ( data[ x + i ] != m_data[ i ] )
			{
				goto skip;
			}
		}

		current_offset += m_data.size();

		return true;

	skip:
		continue;
	}

	return false;
}

DataIdentifierSearch::DataIdentifierSearch( const Json::Value& json ) : MimeDataIdentifier( json[ "data" ] )
{
	auto& offset_v { json[ "offset" ] };
	auto& hex_v { json[ "hex" ] };
	auto& id { json[ "id" ] };

	if ( offset_v.isNull() )
		offset = NO_OFFSET;
	else
		offset = offset_v.asInt();

	if ( hex_v.isNull() ) throw std::invalid_argument( "Failed to parse identifier type: Hex field was null" );

	m_data = decodeHex( hex_v.asString() );
}

bool DataIdentifierOverride::
	pass( const std::byte* data, const std::size_t length, std::size_t& current_offset, MimeDataTrack& context ) const
{
	if ( DataIdentifierSearch::pass( data, length, current_offset, context ) )
	{
		context.overriden_name = override_name;
	}
	else
		log::debug( "Failed to find unique data for {}", override_name );

	return true;
}

DataIdentifierOverride::DataIdentifierOverride( const Json::Value& json ) : DataIdentifierSearch( json )
{
	auto& name { json[ "override" ] };
	if ( name.isNull() ) throw std::invalid_argument( "Failed to parse override name" );
	override_name = name.asString();
}

MimeIdentifier::MimeIdentifier(
	std::string&& mime,
	std::vector< std::string >&& extensions,
	std::vector< std::unique_ptr< MimeDataIdentifier > >&& identifiers ) :
  m_mime( std::forward< std::string >( mime ) ),
  m_extensions( std::forward< std::vector< std::string > >( extensions ) ),
  m_identifiers( std::forward< std::vector< std::unique_ptr< MimeDataIdentifier > > >( identifiers ) )
{}

bool MimeIdentifier::test( const std::byte* data, const std::size_t length, MimeDataTrack& context ) const
{
	for ( const auto& identifier : m_identifiers )
	{
		std::size_t offset { 0 };

		if ( !identifier->test( data, length, offset, context ) ) return false;
	}

	return true;
}

MimeIdentifier MimeIdentifier::loadFromFile( const std::filesystem::path& path )
{
	if ( std::ifstream ifs( path, std::ios::binary | std::ios::ate ); ifs )
	{
		std::vector< char > data {};
		data.resize( ifs.tellg() );

		ifs.seekg( 0, std::ios::beg );
		ifs.read( data.data(), data.size() );

		const std::string text { data.data(), data.size() };

		//Convert to json

		Json::Reader reader {};
		Json::Value root {};

		reader.parse( text, root );

		return loadFromJson( root );
	}

	throw std::runtime_error( std::format( "Failed to load file {}", path.string() ) );
}

MimeIdentifier MimeIdentifier::loadFromJson( const Json::Value& json )
{
	if ( json.isNull() ) throw std::runtime_error( std::format( "Failed to load json: {}", json.asString() ) );

	std::string mime { json[ "mime" ].asString() };
	const auto& extension_array { json[ "extensions" ] };
	if ( !extension_array.isArray() )
		throw std::runtime_error( std::format( "Failed to load json, extensions was not an array of strings" ) );

	std::vector< std::string > extensions {};

	for ( const auto& extension : extension_array )
	{
		extensions.push_back( extension.asString() );
	}

	const auto& parser_data { json[ "data" ] };

	std::vector< std::unique_ptr< MimeDataIdentifier > > identifiers {};
	identifiers.reserve( parser_data.size() );

	for ( const auto& data : parser_data )
	{
		identifiers.push_back( parseIdentifier( data ) );
	}

	MimeIdentifier identifier { std::move( mime ), std::move( extensions ), std::move( identifiers ) };

	return identifier;
}

MimeDatabase::MimeDatabase()
{
	reloadMimeParsers();
}

std::optional< std::string > MimeDatabase::scan( const std::byte* data, const std::size_t length )
{
	if ( updating_flag ) updating_flag.wait( false );

	for ( const auto& identifier : m_identifiers )
	{
		MimeDataTrack context {};

		if ( identifier.test( data, length, context ) )
		{
			if ( !context.overriden_name.empty() ) return context.overriden_name;

			return identifier.m_mime;
		}
	}

	return std::nullopt;
}

void MimeDatabase::reloadMimeParsers()
{
	updating_flag.store( true );

	//TODO: Change this to be in the config
	const auto parser_path { std::filesystem::current_path() / "mime" };

	if ( !std::filesystem::exists( parser_path ) ) std::filesystem::create_directories( parser_path );

	log::info( "Reloading mime parsers from {}", parser_path.string() );

	for ( const auto& file : std::filesystem::recursive_directory_iterator( parser_path ) )
	{
		log::debug( "Loading {}", file.path().string() );

		MimeIdentifier identifier { MimeIdentifier::loadFromFile( file.path() ) };

		log::debug( "Found identifier for mime type {}", identifier.m_mime );

		m_identifiers.emplace_back( std::move( identifier ) );
	}

	log::info( "Loaded {} mime parsers", m_identifiers.size() );

	updating_flag.store( false );
	updating_flag.notify_all();
}

std::shared_ptr< MimeDatabase > getInstance()
{
	if ( mime_db ) return mime_db;

	return mime_db = std::shared_ptr< MimeDatabase >( new MimeDatabase() );
}

} // namespace idhan::mime