#pragma once

#include <ada.h>
#include <optional>
#include <simdjson.h>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "JsonDecodingValues.hpp"

using AdaPattern = ada::url_pattern< ada::url_pattern_regex::std_regex_provider >;

struct SearchParameter
{
	SearchParameter( std::string name, simdjson::dom::element json ) :
	  m_key { std::move( name ) },
	  m_value { idhan::downloader::manifest_json::decode< std::string >( json ) },
	  m_capture { m_value.starts_with( ':' ) }
	{
		if ( m_capture )
		{
			m_value.erase( 0, 1 );
		}
	}

	std::string m_key;
	//! Names the capture if the capture is set. Otherwise this defines the exact value we want
	std::string m_value;
	bool m_capture {};
};

struct SearchPattern
{
	std::vector< SearchParameter > m_queries {};

	bool match( std::string_view key ) const;
	//! Returns true if the search pattern is a capture
	bool is_capture( std::string_view key ) const;

	//! Returns true only if all required search parameters are found
	bool hasRequired( ada::url_search_params& params );
};

struct Pattern
{
	AdaPattern m_ada_pattern {};
	SearchPattern m_search_pattern {};

  public:

	explicit Pattern( simdjson::dom::element json );

	//! Returns true if the url matches the pattern
	bool match( std::string_view url );
	//! Returns true if the query matches.
	bool matchQuery( std::string_view url );
	std::unordered_map< std::string, std::string > captures( std::string_view url );
};
