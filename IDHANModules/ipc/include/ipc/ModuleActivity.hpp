#pragma once

#include <cstddef>

namespace idhan::ipc
{

struct ModuleActivity
{
	//! Includes calls waiting for a worker thread or the module's serialization lock.
	std::size_t queued { 0 };
	//! Includes calls executing module code or waiting for a module callback.
	std::size_t running { 0 };

	bool operator==( const ModuleActivity& ) const = default;
};

} // namespace idhan::ipc
