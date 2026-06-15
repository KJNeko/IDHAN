//
// Created by kj16609 on 12/18/24.
//
#include "MimeDatabase.hpp"

#include <expected>
#include <filesystem>
#include <fstream>
#include <paths.hpp>

#include "filesystem/io/IOUring.hpp"
#include "Cursor.hpp"
#include "MimeIdentifier.hpp"
#include "ModuleBase.hpp"
#include "api/helpers/createBadRequest.hpp"

namespace idhan::mime
{

MimeDatabase::MimeDatabase()
{
	log::trace( "MimeDatabase constructor: deferred initialization to startup" );
}

drogon::Task< std::expected< std::string, drogon::HttpResponsePtr > > MimeDatabase::scan( const Cursor cursor )
{
	log::trace( "MimeDatabase::scan: starting scan with {} identifiers", m_identifiers.size() );
	std::vector< std::pair< std::string, MimeScore > > positive_matches {};

	for ( const auto& identifier : m_identifiers )
	{
		if ( !identifier.hasMatchers() )
		{
			log::trace( "MimeDatabase::scan: skipping identifier {} (no matchers)", identifier.mime() );
			continue;
		}

		log::trace( "MimeDatabase::scan: testing identifier for {}", identifier.mime() );

		if ( co_await identifier.test( cursor ) )
		{
			log::debug(
				"MimeDatabase::scan: identifier {} passed (priority: {})", identifier.mime(), identifier.priority() );
			positive_matches.emplace_back( identifier.mime(), identifier.priority() );
		}
		else
		{
			log::trace( "MimeDatabase::scan: identifier {} did NOT match", identifier.mime() );
		}
	}

	if ( positive_matches.empty() )
	{
		log::debug( "MimeDatabase::scan: no identifiers matched" );
		co_return std::unexpected( createBadRequest( "Could not identify mime from file" ) );
	}
	std::ranges::sort(
		positive_matches,
		[]( const auto& left, const auto& right ) noexcept -> bool { return left.second > right.second; } );

	std::string matches_out {};
	for ( std::size_t i = 0; i < positive_matches.size(); ++i )
	{
		const auto& [ match, score ] = positive_matches[ i ];
		matches_out += format_ns::format( "({}, {})", match, score );
		if ( i + 1 < positive_matches.size() ) matches_out += ", ";
	}

	log::debug( "Found {} positive MIME matches: {}", positive_matches.size(), matches_out );

	const auto first_result { positive_matches.at( 0 ) };

	log::debug( "MimeDatabase::scan: selected mime: {}", first_result.first );
	co_return first_result.first; // first field of the pair is the mime string
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

drogon::Task< std::expected< std::string, drogon::HttpResponsePtr > > MimeDatabase::scan(
	std::string_view data,
	const std::string file_name )
{
	log::trace( "MimeDatabase::scan(string_view, file_name={})", file_name );
	Cursor cursor { data, file_name };
	co_return co_await scan( cursor );
}

drogon::Task< std::expected< std::string, drogon::HttpResponsePtr > > MimeDatabase::scan(
	data_view data,
	const std::string file_name )
{
	log::trace( "MimeDatabase::scan(data_view, file_name={})", file_name );
	const std::string_view data_view { reinterpret_cast< const char* >( data.data() ), data.size() };
	Cursor cursor { data_view, file_name };
	co_return co_await scan( cursor );
}

drogon::Task< std::expected< std::string, drogon::HttpResponsePtr > > MimeDatabase::scan( FileIOUring file_io )
{
	log::trace( "MimeDatabase::scan(FileIOUring, path={})", file_io.path().string() );
	Cursor cursor { file_io };
	co_return co_await scan( cursor );
}

drogon::Task< std::expected< std::string, drogon::HttpResponsePtr > > MimeDatabase::scanFile(
	const std::filesystem::path& path )
{
	log::trace( "MimeDatabase::scanFile(path={})", path.string() );
	FileIOUring io { path };
	co_return co_await scan( io );
}

drogon::Task< std::expected< void, drogon::HttpResponsePtr > > MimeDatabase::reloadMimeParsers()
{
	auto db { drogon::app().getDbClient() };

	m_identifiers.clear();
	const std::vector< std::filesystem::path > paths { getMimeParserPaths() };
	log::trace( "reloadMimeParsers: found {} parser paths", paths.size() );

	for ( const auto& path : paths )
	{
		log::trace( "reloadMimeParsers: loading parser from {}", path.filename().string() );
		try
		{
			auto& identifier { m_identifiers.emplace_back( path ) };

			std::string mime { identifier.mime() };
			log::debug( "reloadMimeParsers: loaded parser for mime type '{}' from {}", mime, path.filename().string() );

			const auto id { co_await getMimeIDFromStr( mime, db ) };

			if ( !id.has_value() )
			{
				log::trace( "reloadMimeParsers: inserting new mime type '{}' into DB", mime );
				co_await db->execSqlCoro(
					"INSERT INTO mime(name, best_extension) VALUES ($1, $2)", mime, identifier.getBestExtension() );
			}
			else
			{
				log::trace( "reloadMimeParsers: mime type '{}' already exists in DB (id={})", mime, *id );
			}
		}
		catch ( std::exception& e )
		{
			log::error( "Failed to load mime parser at {} due to: {}", path.string(), e.what() );
		}
	}

	log::trace( "reloadMimeParsers: completed, {} identifiers loaded", m_identifiers.size() );
	co_return {};
}

std::shared_ptr< MimeDatabase > getMimeDatabase()
{
	static std::shared_ptr< MimeDatabase > instance { std::shared_ptr< MimeDatabase >( new MimeDatabase() ) };

	return instance;
}

drogon::Task< std::expected< MimeID, drogon::HttpResponsePtr > > getMimeIDFromStr( std::string str, DbClientPtr db )
{
	const auto search_result { co_await db->execSqlCoro( "SELECT mime_id FROM mime WHERE name = $1", str ) };

	if ( !search_result.empty() )
	{
		co_return search_result[ 0 ][ 0 ].as< MimeID >();
	}

	co_return std::unexpected( createInternalError( "Could not find mime: {}", str ) );
}
} // namespace idhan::mime
