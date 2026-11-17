#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

struct RequestRate
{
	std::uint16_t m_requests { 1 };
	std::uint16_t m_seconds { 2 };
	bool m_shared_subdomains { false };
	bool m_spread { false };
	std::optional< std::uint32_t > m_concurrency {};
};

struct RequestRateDisabled
{};

using RequestRatePolicy = std::variant< std::monostate, RequestRateDisabled, RequestRate >;

struct URLClassHost
{
	struct Lane
	{
		std::optional< std::uint32_t > m_concurrency {};
		std::vector< std::uint16_t > m_backoff_statuses {};
		std::optional< double > m_keep_alive {};
		std::optional< double > m_error_backoff {};
		std::optional< std::string > m_http_version {};
		std::optional< std::string > m_group {};
	};

	struct Retry
	{
		std::uint16_t m_code {};
		std::uint32_t m_count {};
		std::optional< double > m_wait {};
	};

	std::string name;
	RequestRatePolicy m_request_rate {};
	std::optional< Lane > m_lane {};
	std::vector< Retry > m_retry {};
	std::optional< std::string > m_points_to {};
};

struct Pattern
{
	// An omitted component is unconstrained; an explicit empty string is preserved.
	std::optional< std::string > m_protocol {};
	std::optional< std::string > m_username {};
	std::optional< std::string > m_password {};
	std::optional< std::string > m_hostname {};
	std::optional< std::string > m_port {};
	std::optional< std::string > m_pathname {};
	std::optional< std::string > m_search {};
	std::optional< std::string > m_hash {};
	std::optional< std::string > m_base_url {};

	struct Query
	{
		std::string m_query_name;
		std::string m_value;
	};

	std::vector< Query > m_queries;
};

struct URLPattern
{
	std::string m_name;
	std::optional< std::string > m_generator {};
	std::vector< Pattern > m_patterns;
	std::vector< std::string > m_remove_query {};
};

struct ParameterBase
{
	std::string m_name;
	std::string m_key;
	bool optional { false };
};

struct StringParameter : ParameterBase
{
	//! An empty string means no default.
	std::string m_default {};
	//! empty if no pattern specified
	std::string m_pattern;
};

struct IntegralParameter : ParameterBase
{
	std::optional< std::int64_t > m_default { std::nullopt };
	std::int64_t max { std::numeric_limits< std::int64_t >::max() };
	std::int64_t min { std::numeric_limits< std::int64_t >::min() };
};

using Parameter = std::variant< StringParameter, IntegralParameter >;

struct URLGenerator
{
	std::string m_id;
	std::string m_handler;
	std::string m_name;
	std::vector< Parameter > m_parameters;
};

struct URLClass
{
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

struct ParserManifest
{
	struct Option
	{
		enum class Type
		{
			BOOLEAN,
			NUMBER,
			STRING,
			STRING_ARRAY
		};
		std::string m_name;
		std::string m_description;
		Type m_type {};
		std::variant< bool, double, std::string, std::vector< std::string > > m_default {};
		std::optional< double > m_minimum {};
		bool m_integer { false };
	};

	struct Handler
	{
		std::string m_id;
		std::string m_method;
		std::vector< Parameter > m_parameters {};
	};

	struct Discovery
	{
		std::string m_url_class;
		std::string m_method;
	};

	struct Cookie
	{
		std::string m_name;
		std::string m_value;
	};

	std::uint32_t m_format_version {};
	std::string m_sdk_version;
	std::string m_id;
	std::uint64_t m_version {};
	std::vector< Option > m_options {};
	std::vector< Handler > m_handlers {};
	std::vector< Discovery > m_discoveries {};
	std::vector< Cookie > m_cookies {};
	std::vector< URLClass > m_url_classes {};
};
