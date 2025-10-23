//
// Created by kj16609 on 10/21/25.
//

#include "MimeMatchSearch.hpp"

#include <json/value.h>
#include <logging/log.hpp>

#include <decodeHex.hpp>

#include "../Cursor.hpp"
#include "drogon/utils/coroutine.h"
#include "spdlog/fmt/bin_to_hex.h"

namespace idhan::mime
{

coro::ImmedientTask< bool > MimeMatchSearch::match( Cursor& cursor ) const
{
	if ( m_offset >= 0 && m_offset != NO_OFFSET )
	{
		cursor.jumpTo( m_offset );

		for ( const auto& match : m_match_data )
		{
			const std::string_view match_view { reinterpret_cast< const char* >( match.data() ), match.size() };
			if ( co_await cursor.tryMatch( match_view ) )
			{
				// log::debug( "PASS Match found {} at offset {}", spdlog::to_hex( match_view ), cursor.pos() );
				cursor.inc( match_view.size() );
				co_return true;
			}
			else
			{
				/*
				const auto data { co_await cursor.data( std::min( match.size(), 16ul ) ) };

				if ( data.size() > 0 )
					log::debug(
						"No match found {}\nvs{}",
						spdlog::to_hex( match_view ),
						spdlog::to_hex( data.substr( 0, match.size() ) ) );
				*/
			}
		}

		co_return false;
	}

	for ( const auto& match : m_match_data )
	{
		const std::string_view match_view { reinterpret_cast< const char* >( match.data() ), match.size() };

		// log::debug( "Searching for {}", spdlog::to_hex( match_view ) );
	}

	const auto start_pos { cursor.pos() };

	const auto pos_limit { m_limit == NO_LIMIT ? NO_LIMIT : cursor.pos() + m_limit };
	do {
		if ( cursor.pos() >= pos_limit )
		{
			// log::debug( "Ending search due to limit being hit. Limit was {}. Started at {}", pos_limit, start_pos );
			break;
		}

		for ( const auto& match : m_match_data )
		{
			const std::string_view match_view { reinterpret_cast< const char* >( match.data() ), match.size() };

			if ( co_await cursor.tryMatchInc( match_view ) )
			{
				co_return true;
			}
		}
	}
	while ( cursor.inc() );

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
	else
	{
		m_limit = std::numeric_limits< std::size_t >::max();
	}
}

MimeMatcher MimeMatchSearch::createFromJson( const Json::Value& json )
{
	return std::make_unique< MimeMatchSearch >( json );
}

} // namespace idhan::mime