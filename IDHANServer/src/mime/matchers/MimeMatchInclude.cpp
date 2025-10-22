//
// Created by kj16609 on 10/21/25.
//
#include "MimeMatchInclude.hpp"

#include <json/value.h>

#include "mime/Cursor.hpp"
#include "mime/MimeIdentifier.hpp"
#include "paths.hpp"

namespace idhan::mime
{

MimeMatchInclude::MimeMatchInclude( const Json::Value& json ) : MimeMatchBase( json )
{}

drogon::Task< bool > MimeMatchInclude::match( [[maybe_unused]] Cursor& cursor ) const
{
	co_return true;
}

MimeMatcher MimeMatchInclude::createFromJson( const Json::Value& json )
{
	// This mime matcher will just simply insert the `data` from a file into another
	if ( !json.isMember( "file" ) )
	{
		throw std::runtime_error(
			"MimeMatchInclude::createFromJson: file field not found. Expected a field `file` with a string as the data" );
	}

	const auto desired_filename { json[ "file" ].asString() };

	for ( const auto& mime_file : getMimeParserPaths() )
	{
		if ( mime_file.filename().string() == desired_filename )
		{
			Json::Value file_json { jsonFromFile( mime_file ) };

			if ( !file_json.isMember( "data" ) )
			{
				throw std::runtime_error(
					format_ns::format( "Json being included ({}) was missing data field", desired_filename ) );
			}

			file_json[ "data" ] = json[ "data" ][ "data" ];

			return std::make_unique< MimeMatchInclude >( file_json );
		}
	}

	throw std::runtime_error(
		format_ns::format( "Failed to find mime file for parser include. Expected {}", json[ "file" ].asString() ) );
}
} // namespace idhan::mime