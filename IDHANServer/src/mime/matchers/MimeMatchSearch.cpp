#include "MimeMatchSearch.hpp"

#include <json/value.h>
#include <logging/log.hpp>

#include <decodeHex.hpp>

#include "drogon/utils/coroutine.h"
#include "mime/Cursor.hpp"

namespace idhan::mime
{

drogon::Task< bool > MimeMatchSearch::match( Cursor& cursor ) const
{
	// negative offsets are relative to the end of the data; only an absent offset means scan mode
	if ( m_offset != NO_OFFSET )
	{
		log::trace( "MimeMatchSearch::match: fixed offset mode at {}", m_offset );
		cursor.jumpTo( m_offset );

		for ( std::size_t i = 0; i < m_match_data.size(); ++i )
		{
			const auto& match { m_match_data[ i ] };
			const std::string_view match_view { reinterpret_cast< const char* >( match.data() ), match.size() };
			log::trace( "MimeMatchSearch::match: trying match {} at offset {}", i + 1, cursor.pos() );
			if ( co_await cursor.tryMatch( match_view ) )
			{
				log::trace( "MimeMatchSearch::match: PASS match {} found at offset {}", i + 1, cursor.pos() );
				std::ignore = cursor.inc( match_view.size() );
				co_return true;
			}
			else
			{
				log::trace( "MimeMatchSearch::match: FAIL match {} at offset {}", i + 1, cursor.pos() );
			}
		}

		co_return false;
	}

	log::trace( "MimeMatchSearch::match: scan mode starting at cursor pos={}", cursor.pos() );
	const auto pos_limit { m_limit == NO_LIMIT ? NO_LIMIT : cursor.pos() + m_limit };
	do
	{
		if ( cursor.pos() >= pos_limit )
		{
			log::trace( "MimeMatchSearch::match: ending scan at pos {} (limit {})", cursor.pos(), pos_limit );
			break;
		}

		for ( std::size_t i = 0; i < m_match_data.size(); ++i )
		{
			const auto& match { m_match_data[ i ] };
			const std::string_view match_view { reinterpret_cast< const char* >( match.data() ), match.size() };

			if ( co_await cursor.tryMatchInc( match_view ) )
			{
				log::trace( "MimeMatchSearch::match: scan found match {} at pos {}", i + 1, cursor.pos() );
				co_return true;
			}
		}
	}
	while ( cursor.inc() );

	log::trace( "MimeMatchSearch::match: scan mode exhausted, no match found" );
	co_return false;
}

MimeMatchSearch::MimeMatchSearch( const Json::Value& json ) : MimeMatchBase( json )
{
	if ( !json.isMember( "offset" ) )
	{
		m_offset = NO_OFFSET;
	}
	else if ( json[ "offset" ].isIntegral() )
	{
		m_offset = json[ "offset" ].asInt64();
	}
	else
	{
		throw std::runtime_error( "MimeMatchSearch: offset must be an integer" );
	}

	if ( !json.isMember( "hex" ) )
	{
		throw std::runtime_error( "MimeMatchSearch::MimeMatchSearch: hex not found" );
	}

	if ( json[ "hex" ].isArray() )
	{
		for ( const auto& hex_str : json[ "hex" ] )
		{
			if ( !hex_str.isString() )
				throw std::runtime_error( "MimeMatchSearch: hex array entries must be hex strings" );
			m_match_data.emplace_back( decodeHex( hex_str.asString() ) );
		}
	}
	else if ( json[ "hex" ].isString() )
	{
		m_match_data.emplace_back( decodeHex( json[ "hex" ].asString() ) );
	}
	else
	{
		throw std::runtime_error( "MimeMatchSearch: hex must be a hex string or an array of hex strings" );
	}

	// an empty pattern would either match everything or nothing depending on the data source
	if ( m_match_data.empty() ) throw std::runtime_error( "MimeMatchSearch: hex must contain at least one pattern" );

	for ( const auto& match : m_match_data )
		if ( match.empty() ) throw std::runtime_error( "MimeMatchSearch: hex patterns must not be empty" );

	if ( !json.isMember( "limit" ) )
	{
		m_limit = NO_LIMIT;
	}
	else if ( json[ "limit" ].isIntegral() && json[ "limit" ].asInt64() > 0 )
	{
		m_limit = static_cast< std::size_t >( json[ "limit" ].asInt64() );
	}
	else
	{
		throw std::runtime_error( "MimeMatchSearch: limit must be a positive integer" );
	}
}

MimeMatcher MimeMatchSearch::createFromJson( const Json::Value& json )
{
	return std::make_unique< MimeMatchSearch >( json );
}

} // namespace idhan::mime
