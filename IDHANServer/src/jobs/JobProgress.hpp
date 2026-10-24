#pragma once

#include <json/value.h>

class JobProgress
{
  public:

	virtual ~JobProgress() = default;
	// Implementations provide a consistent snapshot while the job's workers update their state.
	[[nodiscard]] virtual Json::Value snapshot() const = 0;
};
