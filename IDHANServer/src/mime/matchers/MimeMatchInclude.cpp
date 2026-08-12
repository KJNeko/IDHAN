#include "MimeMatchInclude.hpp"

#include <json/value.h>

#include <algorithm>

#include "logging/log.hpp"
#include "mime/Cursor.hpp"
#include "mime/MimeIdentifier.hpp"
#include "paths.hpp"

namespace idhan::mime
{

// Parsing is fully synchronous (no co_await between include expansions), so a thread_local
// stack of the files currently being expanded is enough to detect include cycles.
thread_local std::vector< std::string > include_stack {};

struct IncludeStackGuard
{
	IncludeStackGuard( const std::string& filename ) { include_stack.emplace_back( filename ); }

	~IncludeStackGuard() { include_stack.pop_back(); }

	FGL_DELETE_ALL_RO5( IncludeStackGuard );
};

MimeMatchInclude::MimeMatchInclude( std::vector< MimeMatcher >&& matchers, const Json::Value& json ) :
  MimeMatchBase( json ),
  m_matchers( std::forward< std::vector< MimeMatcher > >( matchers ) )
{}

drogon::Task< bool > MimeMatchInclude::match( Cursor& cursor ) const
{
	log::trace( "MimeMatchInclude::match: entering with {} sub-matchers", m_matchers.size() );
	for ( std::size_t i = 0; i < m_matchers.size(); ++i )
	{
		log::trace( "MimeMatchInclude::match: testing sub-matcher {}/{}", i + 1, m_matchers.size() );
		if ( !co_await m_matchers[ i ]->test( cursor ) )
		{
			log::trace( "MimeMatchInclude::match: sub-matcher {} failed", i + 1 );
			co_return false;
		}
		log::trace( "MimeMatchInclude::match: sub-matcher {} passed", i + 1 );
	}

	log::trace( "MimeMatchInclude::match: all sub-matchers passed" );
	co_return true;
}

MimeMatcher MimeMatchInclude::createFromJson( const Json::Value& json )
{
	if ( !json.isMember( "file" ) )
	{
		throw std::runtime_error(
			"MimeMatchInclude::createFromJson: file field not found. Expected a field `file` with a string as the data" );
	}

	const auto desired_filename { json[ "file" ].asString() };
	log::trace( "MimeMatchInclude::createFromJson: looking for included file '{}'", desired_filename );

	if ( std::ranges::contains( include_stack, desired_filename ) )
	{
		throw std::runtime_error(
			format_ns::format( "Circular mime parser include detected: {} includes itself", desired_filename ) );
	}

	for ( const auto& mime_file : getMimeParserPaths() )
	{
		if ( mime_file.filename().string() == desired_filename )
		{
			log::trace( "MimeMatchInclude::createFromJson: found included file at {}", mime_file.string() );
			const IncludeStackGuard stack_guard { desired_filename };
			Json::Value file_json { jsonFromFile( mime_file ) };

			if ( !file_json.isMember( "data" ) )
			{
				throw std::runtime_error(
					format_ns::format( "Json being included ({}) was missing data field", desired_filename ) );
			}

			std::vector< MimeMatcher > matchers { parseDataJson( file_json[ "data" ] ) };
			log::trace( "MimeMatchInclude::createFromJson: parsed {} matchers from included file", matchers.size() );

			return std::make_unique< MimeMatchInclude >( std::move( matchers ), json );
		}
	}

	throw std::runtime_error(
		format_ns::format( "Failed to find mime file for parser include. Expected {}", json[ "file" ].asString() ) );
}
} // namespace idhan::mime
