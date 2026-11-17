#pragma once

#include <cstdint>
#include <optional>
#include <simdjson.h>
#include <string>
#include <vector>

#include "RequestRate.hpp"
#include "URLClassHost.hpp"
#include "URLGenerator.hpp"
#include "URLPattern.hpp"

struct URLMatch
{
	std::string url_class;
	std::string pattern;
	std::optional< std::string > generator;
	URLCaptures captures;
};

struct URLClass
{
	std::optional< URLMatch > tryMatch( const ada::url_aggregator& url );
	explicit URLClass( simdjson::dom::element json );

	std::string m_id;
	std::uint64_t m_version {};
	std::vector< URLClassHost > m_hosts;
	RequestRatePolicy m_request_rate {};
	std::optional< std::string > m_parser {};
	std::vector< URLPattern > m_url_patterns;
	std::vector< URLGenerator > m_generators;
	bool m_sort_query_parameters { true };
	std::vector< std::string > m_remove_query {};
	std::vector< std::string > m_ephemeral_query {};
};
