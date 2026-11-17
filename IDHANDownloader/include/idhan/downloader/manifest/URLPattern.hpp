#pragma once

#include <optional>
#include <simdjson.h>
#include <string>
#include <vector>

#include "Pattern.hpp"

struct URLPattern
{
	explicit URLPattern( simdjson::dom::element json );
	std::optional< URLCaptures > tryMatch( const ada::url_aggregator& url );

	std::string m_name;
	std::optional< std::string > m_generator {};
	std::vector< Pattern > m_patterns;
	std::vector< std::string > m_remove_query {};
};
