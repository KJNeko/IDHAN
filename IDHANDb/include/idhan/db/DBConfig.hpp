#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

#include "SearchPath.hpp"

namespace idhan::db
{
struct DBConfig
{
	std::string host { "localhost" };
	std::uint16_t port { 5432 };
	std::string database { "idhan-db" };
	std::string user { "idhan" };
	std::string password { "idhan" };
	std::string schema { DEFAULT_SCHEMA };
	std::string extension_schema { EXTENSION_SCHEMA };
	std::size_t connections { 8 };
	std::size_t transaction_connections { 2 };
	std::size_t worker_threads { 0 };
	std::chrono::milliseconds statement_timeout { 60000 };
	std::size_t diagnostic_value_bytes { 4096 };
	std::chrono::milliseconds slow_query_threshold { 1000 };
	bool log_secrets { false };
	void validate() const;
	[[nodiscard]] static DBConfig defaults();
};

enum class ResultFormat : int
{
	text = 0,
	binary = 1
};

struct QueryOptions
{
	bool prepare { false };
	ResultFormat result_format { ResultFormat::binary };
	std::uint64_t request_id { 0 };
};

inline constexpr std::size_t row_prefetch { 256 };
inline constexpr std::size_t pump_operation_budget { 64 };

} // namespace idhan::db
