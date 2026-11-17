#pragma once

#include <cstdint>

namespace idhan
{

enum ImportStatus : std::uint8_t
{
	Success = 1,
	Exists  = 2,
	Deleted = 3,
	Failed  = 4,
};

enum ImportFailureCode : std::uint8_t
{
	UnknownReason = 0,
	UnknownMime   = 1,
};

} // namespace idhan