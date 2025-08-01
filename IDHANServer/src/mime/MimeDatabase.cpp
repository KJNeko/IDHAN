//
// Created by kj16609 on 12/18/24.
//
#include "MimeDatabase.hpp"

#include <json/json.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <cassert>
#include <expected>
#include <fcntl.h>
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

	if ( str == "search" ) return IdentifierType::Search;
	if ( str == "override" ) return IdentifierType::Override;

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

bool DataIdentifierSearch::testOffset(
	const std::byte* data,
	const std::size_t length,
	[[maybe_unused]] std::size_t& cursor,
	[[maybe_unused]] MimeDataTrack& context ) const
{
	for ( const auto& set : m_data )
	{
		if ( set.match( data + offset, length ) )
		{
			cursor = offset + set.size();
			return true;
		}
	}

	return false;
}

bool DataIdentifierSearch::testScanForward(
	const std::byte* data,
	const std::size_t length,
	std::size_t& cursor,
	[[maybe_unused]] MimeDataTrack& context ) const
{
	if ( length <= m_data.size() ) return false;

	std::size_t current_offset { cursor };

	// if any of these are a full match, we use them
	while ( current_offset <= length )
	{
		for ( const auto& set : m_data )
		{
			if ( set.match( data + current_offset, length ) )
			{
				// if we match, set the cursor to be at the end of the match
				cursor = current_offset + set.size();
				return true;
			}
		}

		current_offset += 1;
	}

	return false;
}

bool DataIdentifierSearch::
	pass( const std::byte* data, const std::size_t length, std::size_t& current_offset, MimeDataTrack& context ) const
{
	if ( offset == NO_OFFSET ) return testScanForward( data, length, current_offset, context );

	return testOffset( data, length, current_offset, context );
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

	if ( hex_v.isArray() )
	{
		for ( const auto& hex_str : hex_v )
		{
			if ( !hex_str.isString() )
				throw std::invalid_argument( "Failed to parse identifier type: Hex field was not an array of strings" );
			m_data.emplace_back( decodeHex( hex_str.asString() ) );
		}
	}
	else if ( hex_v.isString() )
	{
		m_data.emplace_back( decodeHex( hex_v.asString() ) );
	}
	else
		throw std::invalid_argument( "Failed to parse identifier type: Hex field was not an array or string" );
}

bool DataIdentifierOverride::
	pass( const std::byte* data, const std::size_t length, std::size_t& current_offset, MimeDataTrack& context ) const
{
	if ( DataIdentifierSearch::pass( data, length, current_offset, context ) )
	{
		context.overriden_name = override_name;
	}

	return true;
}

DataIdentifierOverride::DataIdentifierOverride( const Json::Value& json ) : DataIdentifierSearch( json )
{
	auto& name { json[ "override" ] };
	if ( name.isNull() ) throw std::invalid_argument( "Failed to parse override name" );
	override_name = name.asString();
	m_required = false;
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
	// test all the various identifiers, only one needs to pass
	for ( const auto& identifier : m_identifiers )
	{
		std::size_t offset { 0 };

		if ( !identifier->test( data, length, offset, context ) && identifier->required() )
		{
			return false;
		}
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

std::expected< std::string, std::exception > MimeDatabase::scanFile( const std::filesystem::path& path )
{
	// Initialize data to nullptr
	const std::byte* data { nullptr };

	// mmap the file
	std::error_code ec {};
	if ( !std::filesystem::exists( path, ec ) || std::filesystem::is_directory( path, ec ) )
	{
		return std::unexpected { std::runtime_error { std::format( "Invalid file path: {}", path.string() ) } };
	}

	const int file_descriptor { open( path.c_str(), O_RDONLY ) };
	if ( file_descriptor == -1 )
	{
		return std::unexpected { std::runtime_error {
			std::format( "Failed to open file {}: {}", path.string(), strerror( errno ) ) } };
	}

	struct stat file_stat {};
	if ( fstat( file_descriptor, &file_stat ) == -1 )
	{
		close( file_descriptor );
		return std::unexpected { std::runtime_error {
			std::format( "Failed to get file stats for {}: {}", path.string(), strerror( errno ) ) } };
	}

	if ( file_stat.st_size == 0 )
	{
		close( file_descriptor );
		return std::unexpected { std::runtime_error { std::format( "File is empty: {}", path.string() ) } };
	}

	void* const mapped_region {
		mmap( nullptr, static_cast< size_t >( file_stat.st_size ), PROT_READ, MAP_PRIVATE, file_descriptor, 0 )
	};
	close( file_descriptor ); // File descriptor can be closed after mmap

	if ( mapped_region == MAP_FAILED )
	{
		return std::unexpected { std::runtime_error {
			std::format( "Failed to mmap file {}: {}", path.string(), strerror( errno ) ) } };
	}

	data = static_cast< const std::byte* >( mapped_region ); // Assign pointer to the mapped memory

	if ( auto opt = scan( data, static_cast< std::size_t >( file_stat.st_size ) ) )
	{
		munmap( mapped_region, file_stat.st_size );
		return opt.value();
	}

	munmap( mapped_region, file_stat.st_size );
	return INVALID_MIME_NAME;
}

std::expected< std::string, std::exception > MimeDatabase::scan( const std::byte* data, const std::size_t length )
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

	return INVALID_MIME_NAME;
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

	auto db { drogon::app().getDbClient() };

	for ( const auto& identifier : m_identifiers )
	{
		db->execSqlSync(
			"INSERT INTO mime (name, best_extension) VALUES ($1, $2) ON CONFLICT (name) DO UPDATE set best_extension = $2",
			identifier.m_mime,
			identifier.m_extensions[ 0 ] );
	}

	updating_flag.store( false );
	updating_flag.notify_all();
}

std::shared_ptr< MimeDatabase > getInstance()
{
	if ( mime_db ) return mime_db;

	return mime_db = std::shared_ptr< MimeDatabase >( new MimeDatabase() );
}

drogon::Task< MimeID > getIDForStr( const std::string str, drogon::orm::DbClientPtr db )
{
	const auto search_result { co_await db->execSqlCoro( "SELECT mime_id FROM mime WHERE name = $1", str ) };
	if ( !search_result.empty() ) co_return search_result[ 0 ][ 0 ].as< MimeID >();

	const auto insert_result {
		co_await db->execSqlCoro( "INSERT INTO mime (name) VALUES ($1) RETURNING mime_id", str )
	};
	if ( !insert_result.empty() ) co_return insert_result[ 0 ][ 0 ].as< MimeID >();
}

} // namespace idhan::mime