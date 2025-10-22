//
// Created by kj16609 on 12/18/24.
//
#include "MimeDatabase.hpp"

#include <expected>
#include <filesystem>
#include <fstream>
#include <paths.hpp>

#include "Cursor.hpp"
#include "MimeIdentifier.hpp"
#include "api/helpers/createBadRequest.hpp"
#include "filesystem/IOUring.hpp"

namespace idhan::mime
{
MimeDatabase::MimeDatabase()
{
	reloadMimeParsers();
}

drogon::Task< std::expected< std::string, drogon::HttpResponsePtr > > MimeDatabase::scan( const Cursor cursor )
{
	std::vector< std::pair< std::string, MimeScore > > positive_matches {};

	for ( const auto& identifier : m_identifiers )
	{
		if ( !identifier.hasMatchers() ) continue;

		log::debug( "Testing identifier for {}", identifier.mime() );

		if ( co_await identifier.test( cursor ) )
		{
			log::debug( "Test passed" );
			positive_matches.emplace_back( identifier.mime(), identifier.priority() );
		}
	}

	if ( positive_matches.empty() )
		co_return std::unexpected( createBadRequest( "Could not identify mime from file" ) );
	std::ranges::
		sort( positive_matches, []( const auto& left, const auto& right ) { return left.second > right.second; } );

	std::string matches_out {};
	for ( std::size_t i = 0; i < positive_matches.size(); ++i )
	{
		const auto& [ match, score ] = positive_matches[ i ];
		matches_out += format_ns::format( "({}, {})", match, score );
		if ( i + 1 < positive_matches.size() ) matches_out += ", ";
	}

	log::debug( "Matches: {}", matches_out );

	log::debug( "Found {} positive matches", positive_matches.size() );

	co_return positive_matches.at( 0 ).first;
}

Json::Value MimeDatabase::dump() const
{
	Json::Value json {};

	std::vector< Json::Value > items {};

	for ( const auto& identifier : m_identifiers )
	{
		Json::Value item {};

		item[ "mime" ] = std::string( identifier.mime() );
		item[ "best_extension" ] = identifier.getBestExtension();
		item[ "score" ] = identifier.priority();

		items.emplace_back( item );
	}

	// rank items by items["priority"]
	std::ranges::sort(
		items,
		[]( const Json::Value& left, const Json::Value& right )
		{ return left[ "score" ].asInt() > right[ "score" ].asInt(); } );

	for ( const auto& item : items ) json.append( item );

	return json;
}

drogon::Task< std::expected< std::string, drogon::HttpResponsePtr > > MimeDatabase::scan( std::string_view data )
{
	Cursor cursor { data };
	co_return co_await scan( cursor );
}

drogon::Task< std::expected< std::string, drogon::HttpResponsePtr > > MimeDatabase::scan( FileIOUring file_io )
{
	Cursor cursor { file_io };
	co_return co_await scan( cursor );
}

drogon::Task< std::expected< std::string, drogon::HttpResponsePtr > > MimeDatabase::
	scanFile( const std::filesystem::path& path )
{
	FileIOUring io { path };
	co_return co_await scan( io );
}

void MimeDatabase::reloadMimeParsers()
{
	m_identifiers.clear();
	const std::vector< std::filesystem::path > paths { getMimeParserPaths() };

	for ( const auto& path : paths )
	{
		try
		{
			m_identifiers.emplace_back( path );
		}
		catch ( std::exception& e )
		{
			log::error( "Failed to load mime parser at {} due to: {}", path.string(), e.what() );
		}
	}
}

std::shared_ptr< MimeDatabase > getInstance()
{
	static std::shared_ptr< MimeDatabase > instance { std::shared_ptr< MimeDatabase >( new MimeDatabase() ) };

	return instance;
}

drogon::Task< std::expected< MimeID, drogon::HttpResponsePtr > >
	getIDForStr( std::string str, drogon::orm::DbClientPtr db )
{
	const auto search_result { co_await db->execSqlCoro( "SELECT mime_id FROM mime WHERE name = $1", str ) };

	if ( !search_result.empty() )
	{
		co_return search_result[ 0 ][ 0 ].as< MimeID >();
	}

	const auto insert_result {
		co_await db->execSqlCoro( "INSERT INTO mime (name) VALUES ($1) RETURNING mimd_id", str )
	};

	if ( insert_result.empty() )
	{
		co_return std::unexpected( createInternalError( "Failed to create mime ID for {}", str ) );
	}

	co_return insert_result[ 0 ][ 0 ].as< MimeID >();
}
} // namespace idhan::mime