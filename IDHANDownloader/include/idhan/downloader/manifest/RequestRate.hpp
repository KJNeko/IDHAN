#pragma once

#include <cstdint>
#include <optional>
#include <simdjson.h>
#include <variant>

struct RequestRate
{
	explicit RequestRate( simdjson::dom::element json );

	std::uint16_t m_requests { 1 };
	std::uint16_t m_seconds { 2 };
	bool m_shared_subdomains { false };
	bool m_spread { false };
	std::optional< std::uint32_t > m_concurrency {};
};

struct RequestRateDisabled
{
	explicit RequestRateDisabled( simdjson::dom::element json );
};

using RequestRatePolicy = std::variant< std::monostate, RequestRateDisabled, RequestRate >;
