//
// Created by kj16609 on 10/21/25.
//

#include "MimeMatchSearch.hpp"

#include <json/value.h>
#include <logging/log.hpp>

#include <decodeHex.hpp>

#include "../Cursor.hpp"
#include "drogon/utils/coroutine.h"

namespace idhan::mime
{

drogon::Task< bool > MimeMatchSearch::match( Cursor& cursor ) const
{
	if ( m_offset >= 0 && m_offset != NO_OFFSET )
	{
		cursor.jumpTo( m_offset );

		for ( const auto& data : m_match_data )
		{
			const std::string_view data_view { reinterpret_cast< const char* >( data.data() ), data.size() };
			if ( co_await cursor.tryMatch( data_view ) )
			{
				cursor.inc( data_view.size() );
				co_return true;
			}
		}

		co_return false;
	}

	const auto pos_limit { cursor.pos() + m_limit };
	do {
		for ( const auto& match : m_match_data )
		{
			const std::string_view match_view { reinterpret_cast< const char* >( match.data() ), match.size() };

			if ( co_await cursor.tryMatchInc( match_view ) )
			{
				co_return true;
			}
		}
	}
	while ( cursor.inc() && ( cursor.pos() <= pos_limit ) );

	co_return false;
}

MimeMatchSearch::MimeMatchSearch( const Json::Value& json ) : MimeMatchBase( json )
{
	if ( !json.isMember( "offset" ) )
	{
		m_offset = NO_OFFSET;
	}
	else
	{
		m_offset = json[ "offset" ].asInt();
	}

	if ( !json.isMember( "hex" ) )
	{
		throw std::runtime_error( "MimeMatchSearch::MimeMatchSearch: hex not found" );
	}

	if ( json[ "hex" ].isArray() )
	{
		for ( const auto& hex_str : json[ "hex" ] )
		{
			m_match_data.emplace_back( decodeHex( hex_str.asString() ) );
		}
	}
	else if ( json[ "hex" ].isString() )
	{
		m_match_data.emplace_back( decodeHex( json[ "hex" ].asString() ) );
	}

	if ( json.isMember( "limit" ) )
	{
		m_limit = static_cast< std::size_t >( json[ "limit" ].asInt() );
	}
}

MimeMatcher MimeMatchSearch::createFromJson( const Json::Value& json )
{
	return std::make_unique< MimeMatchSearch >( json );
}

} // namespace idhan::mime