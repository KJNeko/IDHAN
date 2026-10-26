#pragma once
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

#include "HTTPConfig.hpp"

namespace idhan::http
{
struct AccessRecord
{
	RequestID request_id;
	std::string route, method, protocol;
	std::optional< std::uint16_t > status;
	std::chrono::nanoseconds elapsed;
	std::uint64_t bytes_received, bytes_sent;
	bool authenticated, disconnected;
};

class AccessLogger
{
  public:

	void write( AccessRecord );
};
} // namespace idhan::http
