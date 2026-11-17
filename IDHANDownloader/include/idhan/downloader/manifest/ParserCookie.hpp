#pragma once

#include <simdjson.h>
#include <string>

#include "JsonDecodingValues.hpp"

struct ParserCookie
{
	explicit ParserCookie( simdjson::dom::element json ) :
	  m_name { idhan::downloader::manifest_json::read< std::string >( json, "name" ) },
	  m_value { idhan::downloader::manifest_json::read< std::string >( json, "value" ) }
	{}

	std::string m_name;
	std::string m_value;
};
