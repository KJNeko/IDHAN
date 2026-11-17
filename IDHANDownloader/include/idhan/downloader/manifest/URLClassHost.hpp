#pragma once

#include <cstdint>
#include <optional>
#include <simdjson.h>
#include <string>
#include <vector>

#include "RequestRate.hpp"

struct URLClassLane
{
	explicit URLClassLane( simdjson::dom::element json );

	std::optional< std::uint32_t > m_concurrency {};
	std::vector< std::uint16_t > m_backoff_statuses {};
	std::optional< double > m_keep_alive {};
	std::optional< double > m_error_backoff {};
	std::optional< std::string > m_http_version {};
	std::optional< std::string > m_group {};
};

struct URLClassRetry
{
	explicit URLClassRetry( simdjson::dom::element json );

	std::uint16_t m_code {};
	std::uint32_t m_count {};
	std::optional< double > m_wait {};
};

struct URLClassHost
{
	explicit URLClassHost( simdjson::dom::element json );

	std::string name;
	RequestRatePolicy m_request_rate {};
	std::optional< URLClassLane > m_lane {};
	std::vector< URLClassRetry > m_retry {};
	std::optional< std::string > m_points_to {};
};
