#pragma once
#include <boost/uuid/uuid.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#include "IDHANTypes.hpp"

namespace idhan::filesystem
{
// Configuration only. Usage counters and filesystem free space must remain live.
struct ClusterInfo
{
	ClusterID id;
	std::filesystem::path path;
	boost::uuids::uuid uuid;
	bool read_only;
	std::optional< std::string > name;
	std::int16_t ratio;
	std::int64_t size_limit;
};
} // namespace idhan::filesystem
