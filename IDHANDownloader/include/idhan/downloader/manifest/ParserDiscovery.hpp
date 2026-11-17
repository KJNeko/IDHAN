#pragma once

#include <simdjson.h>
#include <string>

#include "JsonDecodingValues.hpp"

struct ParserDiscovery
{
	explicit ParserDiscovery( simdjson::dom::element json ) :
	  m_url_class { idhan::downloader::manifest_json::read< std::string >( json, "urlClass" ) },
	  m_method { idhan::downloader::manifest_json::read< std::string >( json, "method" ) }
	{}

	std::string m_url_class;
	std::string m_method;
};
