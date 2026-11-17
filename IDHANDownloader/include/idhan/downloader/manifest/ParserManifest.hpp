#pragma once

#include <cstdint>
#include <simdjson.h>
#include <string>
#include <vector>

#include "ParserCookie.hpp"
#include "ParserDiscovery.hpp"
#include "ParserHandler.hpp"
#include "ParserOption.hpp"
#include "URLClass.hpp"

struct ParserManifest
{
	explicit ParserManifest( simdjson::dom::element json );

	std::uint32_t m_format_version {};
	std::string m_sdk_version;
	std::string m_id;
	std::uint64_t m_version {};
	std::vector< ParserOption > m_options {};
	std::vector< ParserHandler > m_handlers {};
	std::vector< ParserDiscovery > m_discoveries {};
	std::vector< ParserCookie > m_cookies {};
	std::vector< URLClass > m_url_classes {};
};
